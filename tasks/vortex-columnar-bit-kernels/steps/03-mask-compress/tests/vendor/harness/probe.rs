// SPDX-License-Identifier: Apache-2.0
#![forbid(unsafe_code)]
// Benchmark probe harness for a Vortex checkout.
//
// No third-party crates and no `unsafe`: the PRNG, the hashing and the timing are all
// hand-rolled so the harness only depends on the four `vortex-*` crates under test.
//
// Measurement design:
//   * §1 every timed sample runs on a **freshly drawn** input, so an implementation that
//     memoises a result between calls is measured as work rather than as a cache hit. The
//     draw of repetition `r` of row `row` of step `step` is a pure function of
//     `(seed, step, row, r)` and shares no state with any other row, so the sequence of
//     inputs is byte-identical whichever tree the probe is linked against. The correctness
//     digest accumulates **every** sample's output, not just the last one.
//     Repetition 0 of a step's own rows draws from that step's shared per-step stream
//     instead (see the `step*` functions); `count_range@*` (§3) takes only its buffer from
//     that stream, and the `__e2e` row (§2) is keyed for every repetition.
//   * §2 an extra `__e2e` row: a realistic filter-then-take scan over a Vortex array, timed
//     as one call per sample, printed after the step's own rows.
//   * §3 step 1's `count_range` rows time **one wide range per sample** rather than a batch of
//     short ones, so the aligned middle-byte loop dominates the measurement.
//   * §4 the detected capability envelope is reported on stderr.
//
// Contract notes:
//   * `ns` is the median of `--reps` samples; one sample is one invocation of the row's
//     operation. Where a row has to cover several sub-calls (the ~258 `select` ranks, the
//     three filter densities, the four `intersect_by_rank` shapes) the *batch* is the
//     operation and its digest accumulates over every sub-call.
//   * Digesting happens after each sample's timer has stopped; drawing the next sample's
//     input happens before the next timer starts. Neither is ever charged to `ns`.
//   * All inputs are derived from `--seed` alone, so they are byte-identical no matter which
//     tree the probe is linked against.

use std::cell::RefCell;
use std::time::Instant;

use vortex_array::ArrayRef;
use vortex_array::Canonical;
use vortex_array::IntoArray;
use vortex_array::VortexSessionExecute;
use vortex_array::array_session;
use vortex_array::arrays::BoolArray;
use vortex_array::arrays::PrimitiveArray;
use vortex_buffer::BitBuffer;
use vortex_buffer::collect_bool_words;
use vortex_buffer::collect_bool_words_multiversioned;
use vortex_error::VortexExpect;
use vortex_mask::Mask;

// ---------------------------------------------------------------------------
// splitmix64 — the only source of randomness in the harness.
// ---------------------------------------------------------------------------

struct SplitMix64(u64);

impl SplitMix64 {
    fn new(seed: u64) -> Self {
        Self(seed)
    }

    fn next_u64(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }

    /// Uniform in `[0, 1)`; only used to pick mask densities and `__e2e` values.
    fn next_f64(&mut self) -> f64 {
        (self.next_u64() >> 11) as f64 * (1.0 / (1u64 << 53) as f64)
    }

    fn next_bool(&mut self) -> bool {
        self.next_u64() >> 63 != 0
    }
}

// ---------------------------------------------------------------------------
// Per-repetition input draws (§1).
//
// The stream a row's repetition `r` draws from is keyed on `(seed, step, row, r)` alone:
// no row can observe another row's consumption, and the *order* rows run in cannot move a
// draw. Repetition 0 of a step's own rows draws from that step's shared per-step stream
// instead.
// ---------------------------------------------------------------------------

/// SplitMix64's finalizer over `(seed, step, row, rep)`.
fn draw_key(seed: u64, step: u64, row: u64, rep: usize) -> u64 {
    let mut z = seed
        ^ step.wrapping_mul(0x9E37_79B9_7F4A_7C15)
        ^ row.wrapping_mul(0xC2B2_AE3D_27D4_EB4F)
        ^ (rep as u64).wrapping_mul(0x1656_67B1_9E37_79F9);
    z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
    z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
    z ^ (z >> 31)
}

/// The generator for repetition `rep` (>= 1) of `row` of `step`.
fn draw_rng(seed: u64, step: u64, row: u64, rep: usize) -> SplitMix64 {
    SplitMix64::new(draw_key(seed, step, row, rep))
}

// ---------------------------------------------------------------------------
// FNV-1a 64-bit digest.
// ---------------------------------------------------------------------------

struct Fnv(u64);

impl Fnv {
    fn new() -> Self {
        Fnv(0xcbf2_9ce4_8422_2325)
    }

    fn write_bytes(&mut self, bytes: &[u8]) {
        for &b in bytes {
            self.0 ^= u64::from(b);
            self.0 = self.0.wrapping_mul(0x0000_0100_0000_01b3);
        }
    }

    fn write_u64(&mut self, v: u64) {
        self.write_bytes(&v.to_le_bytes());
    }

    fn write_usize(&mut self, v: usize) {
        self.write_u64(v as u64);
    }

    fn write_option_usize(&mut self, v: Option<usize>) {
        match v {
            Some(x) => {
                self.write_bytes(&[1u8]);
                self.write_usize(x);
            }
            None => self.write_bytes(&[0u8]),
        }
    }

    /// `true_count` and the packed words of a mask. `Mask::values()` is `None` for the
    /// `AllTrue` / `AllFalse` shapes, which `len`/`true_count` already pin down.
    fn write_mask(&mut self, mask: &Mask) {
        self.write_usize(mask.len());
        self.write_usize(mask.true_count());
        if let Some(values) = mask.values() {
            self.write_bytes(values.bit_buffer().inner().as_slice());
        }
    }

    fn hex(&self) -> String {
        format!("{:016x}", self.0)
    }
}

// ---------------------------------------------------------------------------
// Row driver.
// ---------------------------------------------------------------------------

struct Row {
    func: &'static str,
    ns: f64,
    digest: String,
}

/// Times `reps` samples of `op`, takes the median, and accumulates the digest of **every**
/// sample's output. The reported `digest` is therefore over all `reps` samples, in order.
///
/// * `first` is the input of sample 0 (from the step's per-step stream); `redraw(rep)` produces
///   the fresh input of every later sample. Both are `black_box`ed before the call so the
///   optimiser cannot constant-fold the operation or hoist it out of the measurement loop.
/// * `redraw` runs *before* `Instant::now()`, so drawing an input is never charged to the
///   sample, and a memoising implementation cannot answer sample `r` from sample `r - 1`.
/// * `prep` also runs *before* `Instant::now()`. It exists so that operations which mutate a
///   destination buffer receive a **fresh** buffer per timed call without charging the
///   allocation to the measurement. The returned value is handed straight to `op`.
/// * `op` returns the call's output; the harness digests it after the timer stops, so
///   hashing is not part of the timed region either.
fn run_row<I, P, T>(
    func: &'static str,
    reps: usize,
    first: I,
    mut redraw: impl FnMut(usize) -> I,
    mut prep: impl FnMut() -> P,
    mut op: impl FnMut(&I, P) -> T,
    mut digest: impl FnMut(&T, &mut Fnv),
) -> Row {
    let mut samples: Vec<f64> = Vec::with_capacity(reps);
    let mut acc = Fnv::new();
    let mut next: Option<I> = Some(first);

    for rep in 0..reps {
        let input = match next.take() {
            Some(input) => input,
            None => redraw(rep),
        };
        let prepared = prep();
        let t0 = Instant::now();
        let out = std::hint::black_box(op(std::hint::black_box(&input), prepared));
        let elapsed = t0.elapsed();
        samples.push(elapsed.as_nanos() as f64);
        digest(&out, &mut acc);
    }

    samples.sort_by(|a, b| a.partial_cmp(b).expect("durations are never NaN"));
    // `reps` is odd; for an even count take the lower-middle element.
    let median = samples[(reps - 1) / 2];

    Row {
        func,
        ns: median,
        digest: acc.hex(),
    }
}

// ---------------------------------------------------------------------------
// Byte-digest helpers for the vortex-array results.
//
// `PrimitiveArray::to_buffer::<T>()` is the fast accessor; the per-type wrappers exist only
// because the byte view of a `T` needs the concrete type to call `to_le_bytes`/`to_bits`.
// ---------------------------------------------------------------------------

macro_rules! int_hasher {
    ($name:ident, $t:ty) => {
        fn $name(array: &PrimitiveArray, digest: &mut Fnv) {
            let buffer = array.to_buffer::<$t>();
            let len = array.len();
            for value in &buffer.as_slice()[..len] {
                digest.write_bytes(&value.to_le_bytes());
            }
        }
    };
}

int_hasher!(hash_i8, i8);
int_hasher!(hash_i16, i16);
int_hasher!(hash_i32, i32);
int_hasher!(hash_i64, i64);
int_hasher!(hash_u32, u32);
int_hasher!(hash_u64, u64);

macro_rules! float_hasher {
    ($name:ident, $t:ty) => {
        fn $name(array: &PrimitiveArray, digest: &mut Fnv) {
            let buffer = array.to_buffer::<$t>();
            let len = array.len();
            for value in &buffer.as_slice()[..len] {
                digest.write_bytes(&value.to_bits().to_le_bytes());
            }
        }
    };
}

float_hasher!(hash_f32, f32);
float_hasher!(hash_f64, f64);

/// Hash a `BitBuffer`'s packed words, LSB-first, exactly `len.div_ceil(64)` u64s of them.
///
/// Reading the words out of the backing bytes by hand (rather than hashing the raw buffer)
/// keeps the digest independent of any slack in the allocation.
fn hash_packed_words(bits: &BitBuffer, digest: &mut Fnv) {
    let words = bits.len().div_ceil(64);
    for chunk in bits.inner().as_slice().chunks_exact(8).take(words) {
        digest.write_u64(u64::from_le_bytes(
            chunk.try_into().expect("chunks_exact yields 8 bytes"),
        ));
    }
}

fn hash_bool_array(array: &BoolArray, digest: &mut Fnv) {
    let bits = array.clone().into_bit_buffer();
    digest.write_usize(bits.len());
    digest.write_bytes(bits.inner().as_slice());
}

// ---------------------------------------------------------------------------
// Shared input-construction helpers.
// ---------------------------------------------------------------------------

fn random_bitbuffer(rng: &mut SplitMix64, len: usize) -> BitBuffer {
    let bits: Vec<bool> = (0..len).map(|_| rng.next_bool()).collect();
    BitBuffer::from_iter(bits)
}

/// A `len`-bit buffer at `offset` bits into a larger random buffer, i.e. one that carries a
/// non-zero bit offset and a trimmed backing buffer.
fn random_bitbuffer_offset(rng: &mut SplitMix64, len: usize, offset: usize, density: f64) -> BitBuffer {
    let bits: Vec<bool> = (0..len + offset).map(|_| rng.next_f64() < density).collect();
    BitBuffer::from_iter(bits).slice(offset..offset + len)
}

fn random_mask(rng: &mut SplitMix64, len: usize, density: f64) -> Mask {
    let bits: Vec<bool> = (0..len).map(|_| rng.next_f64() < density).collect();
    Mask::from_buffer(BitBuffer::from_iter(bits))
}

/// The cheap, vectorizable predicate used by every step-2 row.
///
/// `(mul, add)` comes from the seed, so the produced words vary with `--seed`; `(mul, add)`
/// of repetition 0 is the per-step `(0x9E37_79B9_7F4A_7C15, salt)`.
#[inline(always)]
fn step2_predicate(i: usize, mul: u64, add: u64) -> bool {
    ((i as u64).wrapping_mul(mul) ^ add) >> 63 != 0
}

// ---------------------------------------------------------------------------
// §4 — capability envelope, on stderr.
// ---------------------------------------------------------------------------

#[cfg(any(target_arch = "x86", target_arch = "x86_64"))]
fn caps_line() -> String {
    format!(
        "caps: sse2={} avx2={} bmi2={} popcnt={} avx512f={}",
        u8::from(is_x86_feature_detected!("sse2")),
        u8::from(is_x86_feature_detected!("avx2")),
        u8::from(is_x86_feature_detected!("bmi2")),
        u8::from(is_x86_feature_detected!("popcnt")),
        u8::from(is_x86_feature_detected!("avx512f")),
    )
}

#[cfg(not(any(target_arch = "x86", target_arch = "x86_64")))]
fn caps_line() -> String {
    "caps: sse2=0 avx2=0 bmi2=0 popcnt=0 avx512f=0".to_string()
}

// ---------------------------------------------------------------------------
// Step 1 — BitBuffer::true_count / count_range
// ---------------------------------------------------------------------------

const S1_SIZES: [usize; 3] = [65536, 8192, 4096];

struct S1True {
    buffer: BitBuffer,
}

struct S1Range {
    buffer: BitBuffer,
    range: (usize, usize),
}

/// A single range that spans essentially the whole buffer: `start` in `[0, margin)` and
/// `end` in `[len - margin, len)`, with `margin = len / 128` — 512 bits for the 65536-bit
/// buffer as §3 asks, and the same proportion for the 8192- and 4096-bit ones. The partial
/// first and last bytes are therefore 1/128th of the call, and the aligned middle-byte loop
/// of `count_ones` dominates what step 1 is about.
fn wide_range(rng: &mut SplitMix64, len: usize) -> (usize, usize) {
    let margin = (len / 128).max(1);
    let start = (rng.next_u64() % margin as u64) as usize;
    let end = len - margin + (rng.next_u64() % margin as u64) as usize;
    (start, end)
}

fn step1(seed: u64, reps: usize) -> Vec<Row> {
    // The per-step stream: the three buffers, in `S1_SIZES` order. Repetition 0 of every
    // step-1 row carries one of them.
    let mut legacy = SplitMix64::new(seed ^ 0x1000_0000_0000_0001);
    let buffers: Vec<BitBuffer> = S1_SIZES.iter().map(|&n| random_bitbuffer(&mut legacy, n)).collect();

    let mut rows = Vec::new();

    for (idx, &len) in S1_SIZES.iter().enumerate() {
        let key: &'static str = match len {
            65536 => "true_count@65536",
            8192 => "true_count@8192",
            _ => "true_count@4096",
        };
        let row = idx as u64;
        rows.push(run_row(
            key,
            reps,
            S1True {
                buffer: buffers[idx].clone(),
            },
            |rep| {
                S1True {
                    buffer: random_bitbuffer(&mut draw_rng(seed, 1, row, rep), len),
                }
            },
            || (),
            |input: &S1True, ()| input.buffer.true_count(),
            |count: &usize, d: &mut Fnv| d.write_usize(*count),
        ));
    }

    for (idx, &len) in S1_SIZES.iter().enumerate() {
        let key: &'static str = match len {
            65536 => "count_range@65536",
            8192 => "count_range@8192",
            _ => "count_range@4096",
        };
        let row = 3 + idx as u64;
        // §3 — one wide range per sample. The range of every sample (including
        // repetition 0) comes from the row's own stream; later repetitions also carry a
        // freshly drawn buffer.
        let first_range = wide_range(&mut draw_rng(seed, 1, row, 0), len);
        rows.push(run_row(
            key,
            reps,
            S1Range {
                buffer: buffers[idx].clone(),
                range: first_range,
            },
            |rep| {
                let mut rng = draw_rng(seed, 1, row, rep);
                let range = wide_range(&mut rng, len);
                S1Range {
                    buffer: random_bitbuffer(&mut rng, len),
                    range,
                }
            },
            || (),
            |input: &S1Range, ()| input.buffer.count_range(input.range.0, input.range.1),
            |count: &usize, d: &mut Fnv| d.write_usize(*count),
        ));
    }

    rows
}

// ---------------------------------------------------------------------------
// Step 2 — bool stream -> packed words
// ---------------------------------------------------------------------------

struct S2 {
    len: usize,
    mul: u64,
    add: u64,
}

/// Repetition `rep`'s predicate parameters for step 2: a fresh multiplier (odd, so the
/// multiply mixes) and a fresh addend per sample, so the `len` produced words differ.
fn s2_params(seed: u64, row: u64, rep: usize) -> (u64, u64) {
    let mut rng = draw_rng(seed, 2, row, rep);
    (rng.next_u64() | 1, rng.next_u64())
}

fn step2(seed: u64, reps: usize) -> Vec<Row> {
    const S2_MUL: u64 = 0x9E37_79B9_7F4A_7C15;

    // The per-step draw: one salt, shared by every step-2 row.
    let salt = seed.wrapping_mul(S2_MUL) ^ 0x2222;

    let mut rows = Vec::new();
    let mut row = 0u64;

    for &len in &[65536usize, 8192] {
        let key: &'static str = match len {
            65536 => "collect_bool_words@65536",
            _ => "collect_bool_words@8192",
        };
        let id = row;
        row += 1;
        let words = len.div_ceil(64);
        rows.push(run_row(
            key,
            reps,
            S2 {
                len,
                mul: S2_MUL,
                add: salt,
            },
            |rep| {
                let (mul, add) = s2_params(seed, id, rep);
                S2 { len, mul, add }
            },
            // Fresh destination buffer per timed call, allocated outside `Instant::now()`
            // and handed to the operation: `collect_bool_words` writes with `=` so a fresh
            // buffer is exactly what the API expects.
            move || vec![0u64; words],
            |input: &S2, mut words: Vec<u64>| {
                collect_bool_words(&mut words, input.len, |i| {
                    step2_predicate(i, input.mul, input.add)
                });
                words
            },
            |words: &Vec<u64>, d: &mut Fnv| {
                for w in words {
                    d.write_u64(*w);
                }
            },
        ));
    }

    for &len in &[65536usize, 8192] {
        let key: &'static str = match len {
            65536 => "collect_bool_words_mv@65536",
            _ => "collect_bool_words_mv@8192",
        };
        let id = row;
        row += 1;
        let words = len.div_ceil(64);
        rows.push(run_row(
            key,
            reps,
            S2 {
                len,
                mul: S2_MUL,
                add: salt,
            },
            |rep| {
                let (mul, add) = s2_params(seed, id, rep);
                S2 { len, mul, add }
            },
            move || vec![0u64; words],
            |input: &S2, mut words: Vec<u64>| {
                collect_bool_words_multiversioned(&mut words, input.len, |i| {
                    step2_predicate(i, input.mul, input.add)
                });
                words
            },
            |words: &Vec<u64>, d: &mut Fnv| {
                for w in words {
                    d.write_u64(*w);
                }
            },
        ));
    }

    // The inherent `BitBuffer` constructors allocate their own packed words internally, so
    // there is no destination buffer to prepare — `prep` is the unit value.
    for &len in &[65536usize, 8192] {
        let key: &'static str = match len {
            65536 => "bitbuffer_collect_bool@65536",
            _ => "bitbuffer_collect_bool@8192",
        };
        let id = row;
        row += 1;
        rows.push(run_row(
            key,
            reps,
            S2 {
                len,
                mul: S2_MUL,
                add: salt,
            },
            |rep| {
                let (mul, add) = s2_params(seed, id, rep);
                S2 { len, mul, add }
            },
            || (),
            |input: &S2, ()| {
                BitBuffer::collect_bool(input.len, |i| step2_predicate(i, input.mul, input.add))
            },
            |bits: &BitBuffer, d: &mut Fnv| {
                // Same digest as the `collect_bool_words` rows above: the packed words.
                hash_packed_words(bits, d);
            },
        ));
    }

    for &len in &[65536usize, 8192] {
        let key: &'static str = match len {
            65536 => "bitbuffer_collect_bool_mv@65536",
            _ => "bitbuffer_collect_bool_mv@8192",
        };
        let id = row;
        row += 1;
        rows.push(run_row(
            key,
            reps,
            S2 {
                len,
                mul: S2_MUL,
                add: salt,
            },
            |rep| {
                let (mul, add) = s2_params(seed, id, rep);
                S2 { len, mul, add }
            },
            || (),
            |input: &S2, ()| {
                BitBuffer::collect_bool_multiversioned(input.len, |i| {
                    step2_predicate(i, input.mul, input.add)
                })
            },
            |bits: &BitBuffer, d: &mut Fnv| {
                hash_packed_words(bits, d);
            },
        ));
    }

    rows
}

// ---------------------------------------------------------------------------
// Step 3 — mask-driven compaction (filter) on PrimitiveArray
// ---------------------------------------------------------------------------

const LEN4: usize = 65536;
const S4_DENSITIES: [f64; 3] = [0.5, 0.8, 0.95];

/// Repetition `rep`'s three filter masks, densities 0.5 / 0.8 / 0.95.
fn s4_masks(seed: u64, row: u64, rep: usize) -> Vec<Mask> {
    let mut rng = draw_rng(seed, 4, row, rep);
    S4_DENSITIES
        .iter()
        .map(|&d| random_mask(&mut rng, LEN4, d))
        .collect()
}

macro_rules! filter_row {
    ($key:literal, $hash:ident, $values:expr, $masks:expr, $fresh:expr, $reps:expr, $ctx:expr) => {{
        let values: ArrayRef = $values;
        let input = (values.clone(), $masks);
        run_row(
            $key,
            $reps,
            input,
            |rep| (values.clone(), $fresh(rep)),
            // Capacity for the three density results, allocated outside the timed region.
            || Vec::<Canonical>::with_capacity(3),
            |input: &(ArrayRef, Vec<Mask>), mut out: Vec<Canonical>| {
                let (array, masks) = input;
                for mask in masks {
                    out.push(
                        array
                            .clone()
                            .filter(mask.clone())
                            .vortex_expect("filter failed")
                            .execute::<Canonical>(&mut $ctx.borrow_mut())
                            .vortex_expect("execute::<Canonical> failed"),
                    );
                }
                out
            },
            |out: &Vec<Canonical>, d: &mut Fnv| {
                for canonical in out {
                    // Re-execute the *result* to its canonical primitive form and hash the
                    // raw buffer, which is the fast accessor for this encoding.
                    let primitive = canonical
                        .clone()
                        .into_array()
                        .execute::<PrimitiveArray>(&mut $ctx.borrow_mut())
                        .vortex_expect("execute::<PrimitiveArray> failed");
                    $hash(&primitive, d);
                }
            },
        )
    }};
}

fn step4(seed: u64, reps: usize) -> Vec<Row> {
    let mut legacy = SplitMix64::new(seed ^ 0x4000_0000_0000_0004);
    let masks: Vec<Mask> = S4_DENSITIES
        .iter()
        .map(|&d| random_mask(&mut legacy, LEN4, d))
        .collect();

    let i8_array = PrimitiveArray::from_iter((0..LEN4).map(|i| i as i8)).into_array();
    let i16_array = PrimitiveArray::from_iter((0..LEN4).map(|i| i as i16)).into_array();
    let i32_array = PrimitiveArray::from_iter((0..LEN4).map(|i| i as i32)).into_array();
    let i64_array = PrimitiveArray::from_iter((0..LEN4).map(|i| i as i64)).into_array();

    let ctx = RefCell::new(array_session().create_execution_ctx());

    vec![
        filter_row!("filter_i8", hash_i8, i8_array, masks.clone(), |rep| s4_masks(seed, 0, rep), reps, ctx),
        filter_row!("filter_i16", hash_i16, i16_array, masks.clone(), |rep| s4_masks(seed, 1, rep), reps, ctx),
        filter_row!("filter_i32", hash_i32, i32_array, masks.clone(), |rep| s4_masks(seed, 2, rep), reps, ctx),
        filter_row!("filter_i64", hash_i64, i64_array, masks.clone(), |rep| s4_masks(seed, 3, rep), reps, ctx),
    ]
}

// ---------------------------------------------------------------------------
// Step 4 — boolean filter
// ---------------------------------------------------------------------------

const LEN6: usize = 65536;

/// One step-6 input: a fresh random bool stream plus the row's density mask.
fn bool_input(rng: &mut SplitMix64, density: f64) -> (ArrayRef, Vec<Mask>) {
    let values = bool_values(rng);
    let mask = random_mask(rng, LEN6, density);
    (values, vec![mask])
}

fn bool_values(rng: &mut SplitMix64) -> ArrayRef {
    let bits = random_bitbuffer(rng, LEN6);
    BoolArray::new(bits, vortex_array::validity::Validity::NonNullable).into_array()
}

macro_rules! bool_filter_row {
    ($key:expr, $first:expr, $fresh:expr, $reps:expr, $ctx:expr) => {{
        run_row(
            $key,
            $reps,
            $first,
            $fresh,
            || Vec::<Canonical>::with_capacity(3),
            |input: &(ArrayRef, Vec<Mask>), mut out: Vec<Canonical>| {
                let (array, masks) = input;
                for mask in masks {
                    out.push(
                        array
                            .clone()
                            .filter(mask.clone())
                            .vortex_expect("filter failed")
                            .execute::<Canonical>(&mut $ctx.borrow_mut())
                            .vortex_expect("execute::<Canonical> failed"),
                    );
                }
                out
            },
            |out: &Vec<Canonical>, d: &mut Fnv| {
                for canonical in out {
                    let boolean = canonical
                        .clone()
                        .into_array()
                        .execute::<BoolArray>(&mut $ctx.borrow_mut())
                        .vortex_expect("execute::<BoolArray> failed");
                    hash_bool_array(&boolean, d);
                }
            },
        )
    }};
}

fn step6(seed: u64, reps: usize) -> Vec<Row> {
    const S6_DENSITIES: [f64; 3] = [0.5, 0.8, 0.95];

    // The per-step draw: the value bits once, then one mask per density, all from a
    // single stream in exactly that order.
    let mut legacy = SplitMix64::new(seed ^ 0x6000_0000_0000_0006);
    let values = bool_values(&mut legacy);
    let legacy_masks: Vec<Mask> = S6_DENSITIES
        .iter()
        .map(|&d| random_mask(&mut legacy, LEN6, d))
        .collect();

    // All three densities sit above `SPARSE_DENSITY_THRESHOLD = 0.05`, so the word-level
    // PEXT path in `arrays/bool/compute/filter.rs` is the one being measured.
    let ctx = RefCell::new(array_session().create_execution_ctx());

    let mut rows = Vec::new();
    for (row, &density) in S6_DENSITIES.iter().enumerate() {
        let key: &'static str = match density {
            0.5 => "bool_filter@0.5",
            0.8 => "bool_filter@0.8",
            _ => "bool_filter@0.95",
        };
        let first = (values.clone(), vec![legacy_masks[row].clone()]);
        rows.push(bool_filter_row!(
            key,
            first,
            |rep| bool_input(&mut draw_rng(seed, 6, row as u64, rep), density),
            reps,
            ctx
        ));
    }
    rows
}

// ---------------------------------------------------------------------------
// Step 5 — rank-based mask intersection
// ---------------------------------------------------------------------------

/// One `intersect_by_rank` shape: `base` density and the `rank` density, plus an optional
/// bit offset for the base buffer (built by slicing a larger random buffer).
struct Shape {
    base_density: f64,
    rank_density: f64,
    offset: Option<usize>,
}

fn build_shape(rng: &mut SplitMix64, shape: &Shape) -> (Mask, Mask, Mask) {
    let len = 65536usize;
    let base = match shape.offset {
        Some(offset) => Mask::from_buffer(random_bitbuffer_offset(
            rng,
            len,
            offset,
            shape.base_density,
        )),
        None => random_mask(rng, len, shape.base_density),
    };
    // `intersect_by_rank` asserts `self.true_count() == mask.len()`.
    let rank_len = base.true_count();
    let rank_a = random_mask(rng, rank_len, shape.rank_density);
    let rank_b = random_mask(rng, rank_len, shape.rank_density);
    (base, rank_a, rank_b)
}

/// The two shapes each step-7 key is exercised with; the dense / sparse-mask pair covers
/// both branches of the (self density, mask density) dispatch, and the offset / sparse-base
/// rows reach the rank-driven path.
const S7_SHAPES: [(&str, [Shape; 2]); 4] = [
    (
        "intersect_by_rank@dense",
        [
            Shape { base_density: 0.5, rank_density: 0.5, offset: None },
            Shape { base_density: 0.8, rank_density: 0.5, offset: None },
        ],
    ),
    (
        "intersect_by_rank@sparse_mask",
        [
            Shape { base_density: 0.5, rank_density: 0.01, offset: None },
            Shape { base_density: 0.8, rank_density: 0.006, offset: None },
        ],
    ),
    (
        "intersect_by_rank@offset",
        [
            Shape { base_density: 0.5, rank_density: 0.5, offset: Some(123) },
            Shape { base_density: 0.5, rank_density: 0.01, offset: Some(123) },
        ],
    ),
    (
        "intersect_by_rank@sparse_base",
        [
            // ~0.02 is what a "sparse base" nominally looks like, but the tree's own
            // `mask_is_sparse` gate is `true_count * 64 < len`, i.e. density < 1/64
            // (~0.0156) — 0.02 falls just *above* it and lands on the bit-buffer path.
            // The second shape is genuinely below the gate so the rank-driven
            // `intersect_by_rank_indices` path is actually reached.
            Shape { base_density: 0.02, rank_density: 0.5, offset: None },
            Shape { base_density: 0.008, rank_density: 0.5, offset: None },
        ],
    ),
];

type S7Input = (Mask, Mask, Mask, Mask, Mask, Mask);

fn step7(seed: u64, reps: usize) -> Vec<Row> {
    let mut legacy = SplitMix64::new(seed ^ 0x7000_0000_0000_0007);

    let mut rows = Vec::new();
    for (idx, (key, pair)) in S7_SHAPES.iter().enumerate() {
        let built: Vec<(Mask, Mask, Mask)> = pair.iter().map(|s| build_shape(&mut legacy, s)).collect();
        for (shape, (base, rank, _)) in pair.iter().zip(built.iter()) {
            let base_offset = base
                .values()
                .map_or(0, |values| values.bit_buffer().offset());
            // The tree's own dispatch gates, so the stderr trace shows which branch each
            // shape selects: `mask_is_sparse` (len * 64 > true_count) picks the rank-driven
            // indices path, `rank_mask_is_sparse` (len * 32 > true_count) the mask-driven one.
            eprintln!(
                "probe: {key}: base_density={} rank_density={} base_len={} base_offset={} base_true_count={} base_is_sparse={} rank_len={} rank_true_count={} rank_is_sparse={}",
                shape.base_density,
                shape.rank_density,
                base.len(),
                base_offset,
                base.true_count(),
                base.true_count().saturating_mul(64) < base.len(),
                rank.len(),
                rank.true_count(),
                rank.true_count().saturating_mul(32) < rank.len(),
            );
        }

        let row = idx as u64;
        let input: S7Input = (
            built[0].0.clone(),
            built[0].1.clone(),
            built[0].2.clone(),
            built[1].0.clone(),
            built[1].1.clone(),
            built[1].2.clone(),
        );

        rows.push(run_row(
            key,
            reps,
            input,
            |rep| {
                let mut rng = draw_rng(seed, 7, row, rep);
                let a = build_shape(&mut rng, &pair[0]);
                let b = build_shape(&mut rng, &pair[1]);
                (a.0, a.1, a.2, b.0, b.1, b.2)
            },
            // `intersect_by_rank(&self, &Mask)` returns a brand new `Mask`; there is no
            // in-place destination buffer, so nothing is prepared before the timer.
            || (),
            |input: &S7Input, ()| {
                [
                    input.0.intersect_by_rank(&input.1),
                    input.0.intersect_by_rank(&input.2),
                    input.3.intersect_by_rank(&input.4),
                    input.3.intersect_by_rank(&input.5),
                ]
            },
            |results: &[Mask; 4], d: &mut Fnv| {
                for mask in results {
                    d.write_mask(mask);
                }
            },
        ));
    }

    rows
}

// ---------------------------------------------------------------------------
// §2 — the `__e2e` row: a filter-then-take scan over a Vortex array.
//
// The workload the ladder exists to speed up: materialise `array.filter(mask)`, then gather
// `take(indices)` out of the filtered result. One timed call is the whole composition.
// ---------------------------------------------------------------------------

const E2E_LEN: usize = 131_072;
const E2E_TAKE: usize = 32_768;
const E2E_MASK_DENSITY: f64 = 0.5;

struct E2eInput {
    array: ArrayRef,
    mask: Mask,
    indices: ArrayRef,
}

/// A freshly drawn `__e2e` input: ~131 k random `f64`s, a ~0.5 mask over them, and ~32 k
/// random indices into the filtered result. The indices are drawn after the mask (their
/// range is the mask's true count), which is input preparation and never timed.
fn e2e_input(seed: u64, rep: usize) -> E2eInput {
    let mut rng = draw_rng(seed, 0, 0, rep);
    let values: Vec<f64> = (0..E2E_LEN).map(|_| rng.next_f64() * 1024.0 - 512.0).collect();
    let array = PrimitiveArray::from_iter(values).into_array();
    let mask = random_mask(&mut rng, E2E_LEN, E2E_MASK_DENSITY);
    let true_count = mask.true_count().max(1);
    let draws: Vec<u64> = (0..E2E_TAKE)
        .map(|_| rng.next_u64() % true_count as u64)
        .collect();
    E2eInput {
        array,
        mask,
        indices: PrimitiveArray::from_iter(draws).into_array(),
    }
}

fn e2e_row(seed: u64, reps: usize) -> Row {
    let ctx = RefCell::new(array_session().create_execution_ctx());

    run_row(
        "__e2e",
        reps,
        e2e_input(seed, 0),
        |rep| e2e_input(seed, rep),
        || (),
        |input: &E2eInput, ()| {
            let filtered = input
                .array
                .clone()
                .filter(input.mask.clone())
                .vortex_expect("filter failed")
                .execute::<Canonical>(&mut ctx.borrow_mut())
                .vortex_expect("execute::<Canonical> failed");
            filtered
                .into_array()
                .take(input.indices.clone())
                .vortex_expect("take failed")
                .execute::<Canonical>(&mut ctx.borrow_mut())
                .vortex_expect("execute::<Canonical> failed")
        },
        |out: &Canonical, d: &mut Fnv| {
            let primitive = out
                .clone()
                .into_array()
                .execute::<PrimitiveArray>(&mut ctx.borrow_mut())
                .vortex_expect("execute::<PrimitiveArray> failed");
            hash_f64(&primitive, d);
        },
    )
}

// ---------------------------------------------------------------------------
// CLI
// ---------------------------------------------------------------------------

fn parse_arg(args: &[String], name: &str) -> Option<String> {
    args.iter()
        .position(|a| a == name)
        .and_then(|i| args.get(i + 1).cloned())
}

fn main() {
    let args: Vec<String> = std::env::args().collect();

    let step: usize = parse_arg(&args, "--step")
        .unwrap_or_else(|| "1".to_string())
        .parse()
        .expect("--step must be a number");
    let reps: usize = parse_arg(&args, "--reps")
        .unwrap_or_else(|| "7".to_string())
        .parse()
        .expect("--reps must be a number");
    let seed: u64 = parse_arg(&args, "--seed")
        .unwrap_or_else(|| "12345".to_string())
        .parse()
        .expect("--seed must be a number");

    assert!(reps >= 1, "--reps must be at least 1");

    eprintln!("probe: step={step} reps={reps} seed={seed}");
    // §4 — the detected capability envelope, so the verifier can assert that it is
    // measuring on a host whose feature set the oracles were written against.
    eprintln!("{}", caps_line());

    // The arm numbers are the LADDER's step numbers, not the order the stages
    // appear upstream. The ladder is five steps cut from seven stages: `select`
    // and `take` are not steps and have no rows, so `simd_compress`, `bool_filter`
    // and `intersect_by_rank` (the `step4`, `step6` and `step7` functions) are
    // steps 3, 4 and 5. The arm numbers must match the SCOPE.step<NN> files.
    let mut rows = match step {
        1 => step1(seed, reps),
        2 => step2(seed, reps),
        3 => step4(seed, reps),
        4 => step6(seed, reps),
        5 => step7(seed, reps),
        other => {
            eprintln!("probe: unknown step {other}");
            std::process::exit(2);
        }
    };

    // §2 — the end-to-end non-regression gate, after the step's own rows.
    rows.push(e2e_row(seed, reps));

    for row in &rows {
        println!(
            "{{\"func\":\"{}\",\"ns\":{:.4},\"digest\":\"{}\"}}",
            row.func, row.ns, row.digest
        );
    }
    println!("{{\"func\":\"__done\",\"ns\":0,\"digest\":\"0000000000000000\"}}");
}
