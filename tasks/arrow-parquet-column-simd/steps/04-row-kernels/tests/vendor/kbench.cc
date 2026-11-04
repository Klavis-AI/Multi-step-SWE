// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

// >>>KB-FAMILY: doc-all
// kbench -- per-function scalar/SIMD measurement harness for the Arrow/Parquet
// column kernels named in the multi_step task.
//
//   kbench dump  <seed> <fnid> [<fnid> ...]   ->  "<fnid> <hex64>"
//   kbench bench <seed> <fnid> [<fnid> ...]   ->  "<fnid> <ns_per_unit> <hex64>"
//
// Every measurement goes through "the implementation the library actually
// installed": the runtime dispatcher (`arrow::internal::unpack<T>`,
// `parquet::internal::DefLevelsToBitmap`, `parquet::internal::GreaterThanBitmap`,
// `parquet::BlockSplitBloomFilter::FindHash`, `ByteStreamSplit*` header entry
// points) or the library's own public/internal entry point with an explicit
// `hardware_flags` argument (`Hashing32::*`, `KeyCompare::*`,
// `SwissTable::early_filter`, `EncoderBinary::Decode`, `EncoderVarBinary::Decode`).
// No named scalar or SIMD symbol is ever called
// directly, so a candidate who optimises the portable path is measured exactly
// like one who writes intrinsics.
//
// Every timed call is handed fresh input bytes (see `Perturber`), so a kernel
// that memoises on its input, or on the buffer address, cannot report the
// cache's cost instead of the kernel's; `dump` makes four such calls and
// checksums the last, so a stale answer fails the bit-exactness comparison.
//
// <<<KB-FAMILY: doc-all
// >>>KB-FAMILY: doc-arm
// ARM NAMING
//   * Functions whose arm is chosen by an explicit `hardware_flags` argument
//     print two lines in every process, `<fnid>@scalar` and `<fnid>@simd`.
//   * Functions that dispatch internally (DynamicDispatch) print one line: the
//     bare `<fnid>` in a default process (that is the *installed* arm -- the
//     runtime-dispatched one) and `<fnid>@scalar` in a process started with
//     ARROW_USER_SIMD_LEVEL=NONE.  The dispatch level is resolved once per
//     process into a function-local static, so the variable must be set before
//     exec; kbench never flips it mid-run, it only *reads* it to label output.
//   * Functions with a single implementation print one line, the bare `<fnid>`.
//
//   So a full arm-pair is obtained by running the binary twice and
//   concatenating:   kbench bench <seed> <ids>            (installed arms)
//                    ARROW_USER_SIMD_LEVEL=NONE kbench bench <seed> <ids>
// <<<KB-FAMILY: doc-arm
//
// >>>KB-FAMILY: anchor
// `anchor.memcpy.<bytes>` are plain memcpy timings over the same sizes the
// kernels touch (unit: ns per byte).  They exist so any reported rate can be
// checked against what the machine can physically do; the harness performs that
// check itself and reports violations on stderr.
// <<<KB-FAMILY: anchor
//
// Inputs are generated from <seed> alone (splitmix64), so every fnid sees the
// same logical input in every process and in every argument order.  Timing
// auto-calibrates the inner repetition count so that one sample is >= 20 ms and
// reports the MEDIAN of kSamples samples; the full min/median/max spread goes to
// stderr.  Output is consumed through a volatile sink after a compiler barrier
// on every iteration, so a call cannot be hoisted out of the timed loop.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "arrow/api.h"
#include "arrow/array/builder_binary.h"
// >>>KB-FAMILY: rowkernels
#include "arrow/compute/key_hash_internal.h"
// <<<KB-FAMILY: rowkernels
// >>>KB-FAMILY: rowkernels
#include "arrow/compute/key_map_internal.h"
// <<<KB-FAMILY: rowkernels
// >>>KB-FAMILY: rowkernels swissjoin
#include "arrow/compute/light_array_internal.h"
// <<<KB-FAMILY: rowkernels swissjoin
// >>>KB-FAMILY: rowkernels
#include "arrow/compute/row/compare_internal.h"
// <<<KB-FAMILY: rowkernels
// >>>KB-FAMILY: rowkernels
#include "arrow/compute/row/encode_internal.h"
// <<<KB-FAMILY: rowkernels
// >>>KB-FAMILY: rowkernels
#include "arrow/compute/row/row_internal.h"
// <<<KB-FAMILY: rowkernels
// >>>KB-FAMILY: rowkernels
#include "arrow/compute/util_internal.h"
// <<<KB-FAMILY: rowkernels
// >>>KB-FAMILY: bloominsert
#include "arrow/io/memory.h"
// <<<KB-FAMILY: bloominsert
#include "arrow/util/bit_util.h"
#include "arrow/util/bitmap_writer.h"
// >>>KB-FAMILY: bpacking
#include "arrow/util/bpacking_internal.h"
// <<<KB-FAMILY: bpacking
// >>>KB-FAMILY: bss
#include "arrow/util/byte_stream_split_internal.h"
// <<<KB-FAMILY: bss
#include "arrow/util/cpu_info.h"
#include "arrow/util/macros.h"

// >>>KB-FAMILY: rowkernels swissjoin
#include "arrow/acero/options.h"
// <<<KB-FAMILY: rowkernels swissjoin
// >>>KB-FAMILY: swissjoin
#include "arrow/acero/swiss_join_internal.h"
// <<<KB-FAMILY: swissjoin
// >>>KB-FAMILY: bloomflt
#include "parquet/bloom_filter.h"
// <<<KB-FAMILY: bloomflt
// >>>KB-FAMILY: levels
#include "parquet/level_comparison.h"
// <<<KB-FAMILY: levels
// >>>KB-FAMILY: levels
#include "parquet/level_conversion.h"
// <<<KB-FAMILY: levels

namespace {

// ---------------------------------------------------------------------------
// build-time knobs (defaults are what the verifier should use)
// ---------------------------------------------------------------------------

// Multiplies the batch size of every fixture.  Default 1.  Recompiling with
// -DKBENCH_SIZE_SCALE=2 is how the "ns/unit is stable when the batch doubles"
// anti-hoisting check is run; it is never needed for a normal measurement.
#ifndef KBENCH_SIZE_SCALE
#  define KBENCH_SIZE_SCALE 1
#endif

// Compiling with -DKBENCH_NO_CONSUME removes the post-call compiler barrier and
// the volatile consumption, which is how the "the number collapses when the
// consumption is removed" check is run.  Never build the shipped artifact with
// it.
#if defined(KBENCH_NO_CONSUME)
#  define KBENCH_CONSUME 0
#else
#  define KBENCH_CONSUME 1
#endif

constexpr int kScale = KBENCH_SIZE_SCALE;
constexpr int kLog2Scale = kScale <= 1 ? 0 : kScale <= 2 ? 1 : kScale <= 4 ? 2 : kScale <= 8 ? 3 : 4;

// Fixture sizes.
// >>>KB-FAMILY: bpacking
constexpr int64_t kBpBatch = 8192 * kScale;        // bpacking: values per call
// <<<KB-FAMILY: bpacking
// >>>KB-FAMILY: bss
constexpr int64_t kBssValues = 16384 * kScale;     // byte-stream-split: values per call
// <<<KB-FAMILY: bss
// >>>KB-FAMILY: levels
constexpr int64_t kLevels = 8192 * kScale;         // levels per call (a "run")
// <<<KB-FAMILY: levels
// >>>KB-FAMILY: bloomflt bloominsert
constexpr int64_t kBloomInserts = 16384;           // hashes inserted into the parquet SBBF
// <<<KB-FAMILY: bloomflt bloominsert
// >>>KB-FAMILY: bloomflt
constexpr int64_t kBloomProbes = 32768 * kScale;   // probes per call
// <<<KB-FAMILY: bloomflt
// >>>KB-FAMILY: rowkernels swissjoin
constexpr int64_t kRows = 4096 * kScale;           // rows per call (row kernels)
// <<<KB-FAMILY: rowkernels swissjoin
// >>>KB-FAMILY: rowkernels
// SwissTable::early_filter only reaches its widest AVX2 kernel
// (early_filter_imp_avx2_x32) when the table has <= 2^4 blocks; above that it
// uses early_filter_imp_avx2_x8 and then a scalar remainder.  4 keeps the
// measured arm on the code path the AVX2 early filter is actually written for.
// Deliberately NOT scaled with kScale, so the batch-doubling check keeps the
// same kernel path.
// <<<KB-FAMILY: rowkernels
constexpr int kSwissLogBlocks = 4;                 // 16 blocks / 128 slots

// Timing.
//
// Both knobs are compile-time overridable so a caller can measure the same
// kernels on the budget a measurement can afford (e.g. 15 samples of ~10 ms):
//
//   -DKBENCH_TARGET_NS=10000000.0 -DKBENCH_SAMPLES=15
//
// The shipped defaults are one sample >= 20 ms, median of 15.
#ifndef KBENCH_TARGET_NS
#  define KBENCH_TARGET_NS 20000000.0
#endif
#ifndef KBENCH_SAMPLES
#  define KBENCH_SAMPLES 15
#endif
constexpr double kTargetSampleNs = KBENCH_TARGET_NS;  // one sample must be >= 20 ms
// At least 7 samples are required and the MEDIAN is what gets printed; 15 is
// used because it makes the median materially more robust when the host is
// oversubscribed (the min/median/max spread is reported on stderr either way).
constexpr int kSamples = KBENCH_SAMPLES;
constexpr int kMaxRecalibrations = 12;
// Calls a `dump` makes per function before checksumming.  More than one so that
// the dump is not a free pass for a kernel that memoises on its input.
constexpr int kDumpCalls = 4;

// ---------------------------------------------------------------------------
// deterministic PRNG
// ---------------------------------------------------------------------------

struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed) {}

  uint64_t Next() {  // splitmix64
    uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }
  uint32_t Next32() { return static_cast<uint32_t>(Next() >> 32); }
  uint64_t Below(uint64_t n) { return n == 0 ? 0 : Next() % n; }
};

// A fnid-dependent seed: the same seed gives the same fixture for a fnid no
// matter which order the fnids appear on the command line.
uint64_t FnidSeed(uint64_t seed, const std::string& fnid) {
  uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a over the fnid
  for (unsigned char c : fnid) {
    h ^= c;
    h *= 0x100000001b3ULL;
  }
  return (seed * 0x9E3779B97F4A7C15ULL) ^ h;
}

// ---------------------------------------------------------------------------
// anti-hoisting: consumption + compiler barrier
// ---------------------------------------------------------------------------

volatile uint64_t g_sink = 0;

// Barrier + consume a memory region.  The barrier has a "memory" clobber, so
// the compiler may neither reorder the kernel's stores before it nor cache the
// loaded words across it; the XOR into the volatile sink makes the loads
// observable.  Together they stop the call from being hoisted out of the timed
// loop or deleted.
inline void ConsumeBytes(const void* p, size_t n) {
#if KBENCH_CONSUME
  asm volatile("" ::: "memory");
  const uint8_t* b = static_cast<const uint8_t*>(p);
  uint64_t acc = 0;
  const size_t words = n / 8;
  if (words >= 8) {
    const size_t quarter = words / 4;
    for (int i = 0; i < 4; ++i) {
      uint64_t v;
      std::memcpy(&v, b + static_cast<size_t>(i) * quarter * 8, 8);
      acc ^= v;
    }
    uint64_t v;
    std::memcpy(&v, b + (words - 1) * 8, 8);
    acc ^= v;
  } else {
    for (size_t i = 0; i + 8 <= n; i += 8) {
      uint64_t v;
      std::memcpy(&v, b + i, 8);
      acc ^= v;
    }
  }
  g_sink = g_sink ^ acc;
#else
  (void)p;
  (void)n;
#endif
}

// Barrier + consume a scalar result.  Used for kernels that return a value
// rather than filling a buffer; a local accumulator folded into the volatile
// sink once per call keeps the cost comparable to ConsumeBytes().
inline void ConsumeScalar(uint64_t v) {
#if KBENCH_CONSUME
  asm volatile("" ::: "memory");
  g_sink = g_sink ^ v;
#else
  (void)v;
#endif
}

// ---------------------------------------------------------------------------
// anti-memoisation: fresh input bytes for every timed call
// ---------------------------------------------------------------------------

// The timing loop calls a kernel thousands of times per sample and takes seven
// samples per process.  Without this, every one of those calls would process the
// SAME input, so a candidate whose kernel memoises -- a cache keyed on the input
// bytes, or on the buffer address, which never changes within a process -- would
// pay the kernel's cost once and report the cache's cost for every measurement
// after it.  Perturb() is called immediately before every timed call (and before
// every call of a `dump`).
//
// Three bytes change per call: the first byte of the input, and two more at
// cursors that walk the buffer and cycle through its head.  One byte is not
// enough: a cache that probes a cheap fixed prefix of the buffer would see the
// same bytes for thousands of calls and answer from its cache in between.  Two
// leave only a smaller cache; three leave a cache that pays for its probe and
// gains nothing.
//
// The mechanism is cheap enough to sit inside the timed region: three XOR
// stores, two compares and two LCG steps, well under the ConsumeBytes()
// barrier every Run() already pays.  It is applied identically to the base and
// the candidate side of every ratio, and the schedule is a pure function of a
// call counter, so both sides feed their kernels the same sequence of inputs
// and the dump checksums still line up.
//
// Families whose input cannot be perturbed byte-wise (the row decoders read an
// encoded row table whose length and offset fields are structural) perturb a
// payload byte of that table, or of the destination offsets derived from it, at
// the same cursors -- see each bench's Perturb().
//
// The one shape this cannot cover is a probe deliberately built against this
// file: one that reads most of the input but skips the bytes the cursors write.
// It stays bit-exact, so no checksum can see it, and its win is bounded by how
// much of the input it skips, because reading nearly all of it costs about what
// the kernel's own read costs.  No in-process perturbation can close that, and
// this one does not claim to.
struct Perturber {
  static constexpr size_t kHeadBytes = 64;
  size_t pos = 0;   // walks the whole buffer, one byte per call
  size_t head = 0;  // cycles inside [0, min(kHeadBytes, n))
  uint64_t state = 0x243F6A8885A308D3ULL;

  // A byte from a full-period LCG: never the same twice in a row, and the
  // sequence written at any one position does not repeat for 2^64 calls.
  uint8_t Value() {
    state = state * 0x5851F42D4C957F2DULL + 0x14057B7EF767814FULL;
    return static_cast<uint8_t>(state >> 58);
  }

  // The three positions this call perturbs and the bytes to XOR in: `a` is the
  // first element of the input (every call), `b` the walking cursor, `c` the
  // head cursor.  Callers whose elements are wider than a byte, or whose values
  // must stay in range, take these and apply the writes themselves.
  struct Step {
    size_t a, b, c;
    uint8_t va, vb, vc;
  };
  Step Next(size_t n) {
    if (n == 0) return Step{0, 0, 0, 0, 0, 0};
    if (pos >= n) pos = 0;
    const size_t h = head < n ? head : 0;
    const uint8_t va = Value(), vb = Value();
    const Step s = {0, pos, h, va, vb, static_cast<uint8_t>(va ^ vb)};
    if (++pos == n) pos = 0;
    if (++head >= kHeadBytes || head >= n) head = 0;
    return s;
  }

  // Perturb a byte buffer at all three cursors.
  void Walk(uint8_t* data, size_t n) {
    if (n == 0) return;
    const Step s = Next(n);
    data[s.a] ^= s.va;
    data[s.b] ^= s.vb;
    data[s.c] ^= s.vc;
    asm volatile("" ::: "memory");
  }
};

// ---------------------------------------------------------------------------
// checksum (deterministic, order independent, byte exact)
// ---------------------------------------------------------------------------

uint64_t ChecksumBytes(const void* p, size_t n) {
  const uint8_t* b = static_cast<const uint8_t*>(p);
  uint64_t h = 0xcbf29ce484222325ULL ^ (static_cast<uint64_t>(n) * 0x100000001b3ULL);
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    uint64_t v;
    std::memcpy(&v, b + i, 8);
    h = (h ^ v) * 0x100000001b3ULL;
  }
  uint64_t tail = 0;
  for (size_t j = i; j < n; ++j) {
    tail |= static_cast<uint64_t>(b[j]) << ((j - i) * 8);
  }
  h = (h ^ tail) * 0x100000001b3ULL;
  h ^= h >> 29;
  h *= 0xBF58476D1CE4E5B9ULL;
  h ^= h >> 32;
  return h;
}

// ---------------------------------------------------------------------------
// fixtures
// ---------------------------------------------------------------------------

// One measurement target.  Constructed once (allocation and input generation
// happen here, outside the timed region), then Run() is called repeatedly.
struct Bench {
  virtual ~Bench() = default;
  virtual void Run() = 0;              // one call + consumption
  virtual uint64_t Checksum() = 0;     // output witness, taken after timing
  virtual int64_t units() const = 0;   // work units per call
  virtual double bytes_per_unit() const = 0;  // traffic estimate, < 0 = unknown
  // Hand the kernel input no earlier call has seen; called immediately before
  // every timed call and before every call of a `dump`.  See `Perturber`.
  // The one bench that is not perturbed is the physics anchor, whose whole job
  // is to measure this machine's memcpy rate and which is never measured.
  virtual void Perturb() {}
  Perturber perturber;
};

// >>>KB-FAMILY: bpacking
// --- bpacking --------------------------------------------------------------
// Unit: one decoded value.
template <typename T>
struct BpBench : Bench {
  int bit_width;
  std::vector<uint8_t> in;
  std::vector<T> out;
  arrow::internal::UnpackOptions opts;

  BpBench(uint64_t seed, int bw) : bit_width(bw) {
    Rng r(seed);
    const size_t max_bytes = static_cast<size_t>(kBpBatch) * sizeof(T);
    in.resize(max_bytes + 64);
    for (size_t i = 0; i < in.size(); ++i) in[i] = static_cast<uint8_t>(r.Next32());
    out.assign(static_cast<size_t>(kBpBatch), T(0));
    opts.batch_size = static_cast<int>(kBpBatch);
    opts.bit_width = bw;
    opts.bit_offset = 0;
    opts.max_read_bytes = -1;
  }

  void Run() override {
    arrow::internal::unpack<T>(in.data(), out.data(), opts);
    ConsumeBytes(out.data(), out.size() * sizeof(T));
  }
  // Fresh bytes of the packed input stream.
  void Perturb() override { perturber.Walk(in.data(), in.size()); }
  uint64_t Checksum() override { return ChecksumBytes(out.data(), out.size() * sizeof(T)); }
  int64_t units() const override { return kBpBatch; }
  double bytes_per_unit() const override {
    return static_cast<double>(bit_width) / 8.0 + static_cast<double>(sizeof(T));
  }
};

// <<<KB-FAMILY: bpacking
// >>>KB-FAMILY: bss
// --- byte stream split -----------------------------------------------------
// Unit: one value (width bytes in, width bytes out).
struct BssBench : Bench {
  int width;
  bool encode;
  std::vector<uint8_t> raw, enc, out;

  BssBench(uint64_t seed, int w, bool enc_) : width(w), encode(enc_) {
    Rng r(seed);
    const size_t bytes = static_cast<size_t>(kBssValues) * static_cast<size_t>(w);
    raw.resize(bytes + 64);
    enc.resize(bytes + 64);
    out.resize(bytes + 64, 0);
    for (size_t i = 0; i < raw.size(); ++i) raw[i] = static_cast<uint8_t>(r.Next32());
    for (size_t i = 0; i < enc.size(); ++i) enc[i] = static_cast<uint8_t>(r.Next32());
  }

  void Run() override {
    if (encode) {
      arrow::util::internal::ByteStreamSplitEncode(raw.data(), width, kBssValues,
                                                   out.data());
    } else {
      arrow::util::internal::ByteStreamSplitDecode(enc.data(), width, kBssValues,
                                                   /*stride=*/kBssValues, out.data());
    }
    ConsumeBytes(out.data(), static_cast<size_t>(kBssValues) * static_cast<size_t>(width));
  }
  // Fresh bytes of whichever side of the codec this bench reads.
  void Perturb() override {
    if (encode) {
      perturber.Walk(raw.data(), raw.size());
    } else {
      perturber.Walk(enc.data(), enc.size());
    }
  }
  uint64_t Checksum() override {
    return ChecksumBytes(out.data(),
                         static_cast<size_t>(kBssValues) * static_cast<size_t>(width));
  }
  int64_t units() const override { return kBssValues; }
  double bytes_per_unit() const override { return 2.0 * width; }
};

// <<<KB-FAMILY: bss
// >>>KB-FAMILY: levels
// --- parquet def-levels -> validity bitmap ---------------------------------
// Unit: one definition level.
struct DefBitmapBench : Bench {
  parquet::internal::LevelInfo info;
  std::vector<int16_t> levels;
  std::vector<uint8_t> valid_bits;

  DefBitmapBench(uint64_t seed, bool repeated) {
    Rng r(seed);
    levels.resize(static_cast<size_t>(kLevels));
    if (repeated) {
      // list<int32>: 0 = null list, 1 = empty list, 2 = null element,
      // 3 = present element.  A realistic run has long stretches of
      // rep_level == max (continuations) punctuated by new lists.
      info = parquet::internal::LevelInfo(/*null_slots=*/1, /*def=*/3, /*rep=*/1,
                                          /*repeated_ancestor_def=*/2);
      for (size_t i = 0; i < levels.size(); ++i) {
        uint32_t v = r.Next32() % 100;
        levels[i] = static_cast<int16_t>(v < 8 ? 0 : v < 20 ? 1 : v < 32 ? 2 : 3);
      }
    } else {
      // optional int32: 0 = null, 1 = present.
      info = parquet::internal::LevelInfo(/*null_slots=*/1, /*def=*/1, /*rep=*/0,
                                          /*repeated_ancestor_def=*/0);
      for (size_t i = 0; i < levels.size(); ++i) {
        levels[i] = static_cast<int16_t>((r.Next32() % 100) < 30 ? 0 : 1);
      }
    }
    valid_bits.assign((static_cast<size_t>(kLevels) + 7) / 8, 0);
  }

  void Run() override {
    std::fill(valid_bits.begin(), valid_bits.end(), 0);
    parquet::internal::ValidityBitmapInputOutput out;
    out.values_read_upper_bound = kLevels;
    out.values_read = 0;
    out.null_count = 0;
    out.valid_bits = valid_bits.data();
    out.valid_bits_offset = 0;
    parquet::internal::DefLevelsToBitmap(levels.data(), kLevels, info, &out);
    ConsumeBytes(valid_bits.data(), valid_bits.size());
  }
  // Three definition levels, low bit only, so each value stays in range.
  void Perturb() override {
    Perturber::Step s = perturber.Next(levels.size());
    levels[s.a] ^= 1;
    levels[s.b] ^= 1;
    levels[s.c] ^= 1;
  }
  uint64_t Checksum() override { return ChecksumBytes(valid_bits.data(), valid_bits.size()); }
  int64_t units() const override { return kLevels; }
  double bytes_per_unit() const override { return 2.0; }  // int16 level read
};

// --- parquet level comparison (> threshold -> bitmap) ----------------------
// Unit: one level.  GreaterThanBitmap produces a 64-bit mask, so the run is
// compared in 64-level windows exactly the way DefLevelsToBitmap uses it.
struct GreaterThanBench : Bench {
  std::vector<int16_t> levels;
  std::vector<uint64_t> masks;
  int64_t windows;

  GreaterThanBench(uint64_t seed, int16_t rhs) : rhs_(rhs) {
    Rng r(seed);
    levels.resize(static_cast<size_t>(kLevels));
    for (size_t i = 0; i < levels.size(); ++i) {
      levels[i] = static_cast<int16_t>(r.Next32() % 4);
    }
    windows = kLevels / 64;
    masks.assign(static_cast<size_t>(windows), 0);
  }

  void Run() override {
    for (int64_t w = 0; w < windows; ++w) {
      masks[static_cast<size_t>(w)] = parquet::internal::GreaterThanBitmap(
          levels.data() + w * 64, 64, rhs_);
      ConsumeScalar(masks[static_cast<size_t>(w)]);
    }
  }
  // Three levels, low bit only, so each value stays in range.
  void Perturb() override {
    Perturber::Step s = perturber.Next(levels.size());
    levels[s.a] ^= 1;
    levels[s.b] ^= 1;
    levels[s.c] ^= 1;
  }
  uint64_t Checksum() override {
    return ChecksumBytes(masks.data(), masks.size() * sizeof(uint64_t));
  }
  int64_t units() const override { return kLevels; }
  double bytes_per_unit() const override { return 2.0 + 8.0 / 64.0; }

  int16_t rhs_;
};

// <<<KB-FAMILY: levels
// >>>KB-FAMILY: bloomflt
// --- parquet split-block bloom filter --------------------------------------
// Unit: one probe (find) / one hash (insert).
struct SbbfFindBench : Bench {
  std::unique_ptr<parquet::BlockSplitBloomFilter> filter;
  std::vector<uint64_t> probes;
  std::vector<uint8_t> hits;

  SbbfFindBench(uint64_t seed) {
    Rng r(seed);
    auto* pool = arrow::default_memory_pool();
    filter = std::make_unique<parquet::BlockSplitBloomFilter>(pool);
    filter->Init(parquet::BlockSplitBloomFilter::OptimalNumOfBytes(kBloomInserts, 0.01));
    // Deterministic, realistic hashes: 64-bit values with a common high half.
    std::vector<uint64_t> inserted(static_cast<size_t>(kBloomInserts));
    for (size_t i = 0; i < inserted.size(); ++i) {
      inserted[i] = (r.Next() & 0x0000FFFFFFFFFFFFULL) | 0x10000000000ULL;
    }
    filter->InsertHashes(inserted.data(), static_cast<int>(inserted.size()));

    probes.resize(static_cast<size_t>(kBloomProbes));
    for (size_t i = 0; i < probes.size(); ++i) {
      // Half of the probes are present in the filter, half are not.
      if ((i & 1) == 0) {
        probes[i] = inserted[i % inserted.size()];
      } else {
        probes[i] = (r.Next() & 0x0000FFFFFFFFFFFFULL) | 0x20000000000ULL;
      }
    }
    hits.assign(probes.size(), 0);
  }

  void Run() override {
    uint64_t acc = 0;
    for (size_t i = 0; i < probes.size(); ++i) {
      const bool found = filter->FindHash(probes[i]);
      asm volatile("" ::: "memory");
      const uint8_t b = found ? 1 : 0;
      hits[i] = b;
      acc += b;
    }
    ConsumeScalar(acc);
  }
  uint64_t Checksum() override { return ChecksumBytes(hits.data(), hits.size()); }
  int64_t units() const override { return static_cast<int64_t>(probes.size()); }
  double bytes_per_unit() const override { return 8.0 + 32.0; }  // probe + one 256-bit block
  // Fresh bytes of the probe hashes.
  void Perturb() override {
    perturber.Walk(reinterpret_cast<uint8_t*>(probes.data()),
                   probes.size() * sizeof(uint64_t));
  }
};

// <<<KB-FAMILY: bloomflt
// >>>KB-FAMILY: bloominsert
struct SbbfInsertBench : Bench {
  std::unique_ptr<parquet::BlockSplitBloomFilter> filter;
  std::vector<uint64_t> hashes;

  SbbfInsertBench(uint64_t seed) {
    Rng r(seed);
    auto* pool = arrow::default_memory_pool();
    filter = std::make_unique<parquet::BlockSplitBloomFilter>(pool);
    filter->Init(parquet::BlockSplitBloomFilter::OptimalNumOfBytes(kBloomInserts, 0.01));
    hashes.resize(static_cast<size_t>(kBloomInserts));
    for (size_t i = 0; i < hashes.size(); ++i) {
      hashes[i] = (r.Next() & 0x0000FFFFFFFFFFFFULL) | 0x10000000000ULL;
    }
  }

  void Run() override {
    filter->InsertHashes(hashes.data(), static_cast<int>(hashes.size()));
    // InsertHashes writes the bitset; the visible state is the filter itself.
    ConsumeScalar(filter->GetBitsetSize());
  }
  uint64_t Checksum() override {
    // Public route to the bitset: serialize it and checksum the bytes.
    auto* pool = arrow::default_memory_pool();
    auto sink_res = arrow::io::BufferOutputStream::Create(
        static_cast<int64_t>(filter->GetBitsetSize()) + 64, pool);
    if (!sink_res.ok()) return 0;
    auto sink = std::move(sink_res).ValueOrDie();
    filter->WriteTo(sink.get());
    auto buf = std::move(sink->Finish()).ValueOrDie();
    return ChecksumBytes(buf->data(), static_cast<size_t>(buf->size()));
  }
  int64_t units() const override { return static_cast<int64_t>(hashes.size()); }
  double bytes_per_unit() const override { return 8.0 + 32.0; }
  // Fresh bytes of the inserted hashes.
  void Perturb() override {
    perturber.Walk(reinterpret_cast<uint8_t*>(hashes.data()),
                   hashes.size() * sizeof(uint64_t));
  }
};

// <<<KB-FAMILY: bloominsert
// >>>KB-FAMILY: rowkernels
// --- row kernels -----------------------------------------------------------

// Deterministic two-column key batch: fixed_size_binary(fixed_width) with no
// nulls and a utf8 column with varying string lengths (1..24 bytes) and nulls.
// Rows with mixed short/long strings are what the var-length compare and hashing
// paths see in a real join.
// <<<KB-FAMILY: rowkernels
// >>>KB-FAMILY: rowkernels swissjoin
struct RowFixture {
  int64_t num_rows = kRows;
  // Width of the fixed-size-binary key column, in bytes.  The row-decode bench
  // raises it to 16: EncoderBinary::Decode routes fixed widths of 0/1/2/4/8
  // (and booleans) to EncoderInteger::Decode, so only a wider key reaches the
  // binary copy path.  16-byte keys are what the library's own row/grouper
  // tests use.
  int64_t fixed_width = 8;
  std::vector<uint8_t> fix_data;
  std::vector<uint8_t> str_data;
  std::vector<int32_t> str_offsets;
  std::vector<uint8_t> validity;
  std::shared_ptr<arrow::Array> fix_array, str_array;
  arrow::ExecBatch batch;
  std::vector<arrow::compute::KeyColumnArray> cols;
  std::vector<arrow::compute::KeyColumnMetadata> metas;

  explicit RowFixture(uint64_t seed, int64_t fixed_width_in = 8) {
    fixed_width = fixed_width_in;
    Rng r(seed);
    const int64_t n = num_rows;
    fix_data.resize(static_cast<size_t>(n) * static_cast<size_t>(fixed_width));
    for (size_t i = 0; i < fix_data.size(); ++i) {
      fix_data[i] = static_cast<uint8_t>(r.Next32());
    }
    validity.assign((static_cast<size_t>(n) + 7) / 8, 0);
    str_offsets.resize(static_cast<size_t>(n) + 1, 0);
    std::vector<uint8_t> str;
    str.reserve(static_cast<size_t>(n) * 12);
    for (int64_t i = 0; i < n; ++i) {
      const bool is_null = (r.Next32() % 5) == 0;
      int64_t len = 0;
      if (!is_null) {
        len = 1 + static_cast<int64_t>(r.Below(24));
        for (int64_t j = 0; j < len; ++j) str.push_back(static_cast<uint8_t>(r.Next32()));
      }
      // Null rows carry a zero-length slice, the canonical representation.
      str_offsets[static_cast<size_t>(i) + 1] = str_offsets[static_cast<size_t>(i)] +
                                                static_cast<int32_t>(len);
    }
    str_data = std::move(str);

    {
      // The validity bitmap is exactly "the string slice is empty", which is how
      // the nulls were generated above; deriving it from the offsets keeps the
      // two columns consistent without replaying the PRNG.
      arrow::internal::BitmapWriter bw(validity.data(), 0, n);
      for (int64_t i = 0; i < n; ++i) {
        const bool is_null =
            (str_offsets[static_cast<size_t>(i) + 1] == str_offsets[static_cast<size_t>(i)]);
        if (is_null) {
          bw.Clear();
        } else {
          bw.Set();
        }
        bw.Next();
      }
      bw.Finish();
    }

    auto fix_ad = arrow::ArrayData::Make(
        arrow::fixed_size_binary(static_cast<int32_t>(fixed_width)), n,
        {nullptr, arrow::Buffer::Wrap(fix_data.data(), fix_data.size()), nullptr}, 0);
    fix_array = arrow::MakeArray(fix_ad);
    auto str_ad = arrow::ArrayData::Make(
        arrow::utf8(), n,
        {arrow::Buffer::Wrap(validity.data(), validity.size()),
         arrow::Buffer::Wrap(str_offsets.data(),
                             str_offsets.size() * sizeof(int32_t)),
         arrow::Buffer::Wrap(str_data.data(), str_data.size())},
        /*null_count=*/-1);
    str_array = arrow::MakeArray(str_ad);
    batch = arrow::ExecBatch({fix_array, str_array}, n);
    (void)arrow::compute::ColumnMetadatasFromExecBatch(batch, &metas);
    (void)arrow::compute::ColumnArraysFromExecBatch(batch, &cols);
  }
};

// Unit: one row.
// <<<KB-FAMILY: rowkernels swissjoin
// >>>KB-FAMILY: rowkernels
struct HashBatchBench : Bench {
  std::unique_ptr<RowFixture> fx;
  std::vector<uint32_t> hashes;
  arrow::util::TempVectorStack stack;
  int64_t flags;

  HashBatchBench(uint64_t seed, int64_t hw) : flags(hw) {
    fx = std::make_unique<RowFixture>(seed);
    hashes.assign(static_cast<size_t>(kRows), 0);
    (void)stack.Init(arrow::default_memory_pool(),
                     arrow::compute::Hashing32::kHashBatchTempStackUsage);
  }
  void Run() override {
    auto st = arrow::compute::Hashing32::HashBatch(fx->batch, hashes.data(), fx->cols, flags,
                                                   &stack, 0, kRows);
    (void)st;
    ConsumeBytes(hashes.data(), hashes.size() * sizeof(uint32_t));
  }
  // Fresh bytes of the fixed-size key column and of the utf8 values: both are
  // rows the kernel reads, and neither is a length or offset field.
  void Perturb() override {
    perturber.Walk(fx->fix_data.data(), fx->fix_data.size());
    perturber.Walk(fx->str_data.data(), fx->str_data.size());
  }
  uint64_t Checksum() override {
    return ChecksumBytes(hashes.data(), hashes.size() * sizeof(uint32_t));
  }
  int64_t units() const override { return kRows; }
  double bytes_per_unit() const override { return 20.0; }
};

struct HashMultiColumnBench : Bench {
  std::unique_ptr<RowFixture> fx;
  std::vector<uint32_t> hashes;
  arrow::util::TempVectorStack stack;
  arrow::compute::LightContext ctx;
  int64_t flags;

  HashMultiColumnBench(uint64_t seed, int64_t hw) : flags(hw) {
    fx = std::make_unique<RowFixture>(seed);
    hashes.assign(static_cast<size_t>(kRows), 0);
    (void)stack.Init(arrow::default_memory_pool(),
                     arrow::compute::Hashing32::kHashBatchTempStackUsage);
    ctx.hardware_flags = flags;
    ctx.stack = &stack;
  }
  void Run() override {
    ctx.hardware_flags = flags;
    arrow::compute::Hashing32::HashMultiColumn(fx->cols, &ctx, hashes.data());
    ConsumeBytes(hashes.data(), hashes.size() * sizeof(uint32_t));
  }
  // Fresh bytes of the fixed-size key column and of the utf8 values.
  void Perturb() override {
    perturber.Walk(fx->fix_data.data(), fx->fix_data.size());
    perturber.Walk(fx->str_data.data(), fx->str_data.size());
  }
  uint64_t Checksum() override {
    return ChecksumBytes(hashes.data(), hashes.size() * sizeof(uint32_t));
  }
  int64_t units() const override { return kRows; }
  double bytes_per_unit() const override { return 20.0; }
};

struct CompareBench : Bench {
  std::unique_ptr<RowFixture> fx;
  std::vector<uint8_t> left_fix, left_str, left_validity;
  std::vector<int32_t> left_offsets;
  std::shared_ptr<arrow::Array> left_fix_array, left_str_array;
  arrow::ExecBatch left_batch;
  std::vector<arrow::compute::KeyColumnArray> left_cols;
  arrow::compute::RowTableImpl row_table;
  std::vector<uint8_t> match;
  std::vector<uint32_t> left_to_right;
  arrow::util::TempVectorStack stack;
  arrow::compute::LightContext ctx;
  int64_t flags;

  CompareBench(uint64_t seed, int64_t hw) : flags(hw) {
    fx = std::make_unique<RowFixture>(seed);
    auto* pool = arrow::default_memory_pool();
    const int64_t n = fx->num_rows;

    arrow::compute::RowTableMetadata table_meta;
    table_meta.FromColumnMetadataVector(fx->metas, sizeof(uint64_t), sizeof(uint64_t));
    (void)row_table.Init(pool, table_meta);
    arrow::compute::RowTableEncoder encoder;
    encoder.Init(fx->metas, sizeof(uint64_t), sizeof(uint64_t));
    encoder.PrepareEncodeSelected(0, n, fx->cols);
    std::vector<uint16_t> row_ids(static_cast<size_t>(n));
    std::iota(row_ids.begin(), row_ids.end(), 0);
    (void)encoder.EncodeSelected(&row_table, static_cast<uint32_t>(n), row_ids.data());

    // Left side: the same logical rows with ~1/7 of them perturbed, and a few
    // turned into nulls, so the comparison is a realistic mix of matches and
    // mismatches rather than a trivially all-equal run.
    left_fix = fx->fix_data;
    for (int64_t i = 0; i < n; i += 7) {
      left_fix[static_cast<size_t>(i) * static_cast<size_t>(fx->fixed_width)] ^= 0x5a;
    }
    left_str = fx->str_data;
    left_offsets = fx->str_offsets;
    left_validity = fx->validity;
    for (int64_t i = 0; i < n; ++i) {
      if ((i % 5) == 0 && i % 11 != 0) {
        const int32_t lo = left_offsets[static_cast<size_t>(i)];
        const int32_t hi = left_offsets[static_cast<size_t>(i) + 1];
        if (hi > lo) left_str[static_cast<size_t>(lo + (hi - lo) / 2)] ^= 0x33;
      }
      if ((i % 11) == 0) {
        left_validity[static_cast<size_t>(i) / 8] &=
            static_cast<uint8_t>(~(1u << (static_cast<size_t>(i) % 8)));
      }
    }
    {
      // Recompute null_count from the (possibly modified) validity bitmap.
      auto fix_ad = arrow::ArrayData::Make(
          arrow::fixed_size_binary(8), n,
          {nullptr, arrow::Buffer::Wrap(left_fix.data(), left_fix.size()), nullptr}, 0);
      left_fix_array = arrow::MakeArray(fix_ad);
      auto str_ad = arrow::ArrayData::Make(
          arrow::utf8(), n,
          {arrow::Buffer::Wrap(left_validity.data(), left_validity.size()),
           arrow::Buffer::Wrap(left_offsets.data(), left_offsets.size() * sizeof(int32_t)),
           arrow::Buffer::Wrap(left_str.data(), left_str.size())},
          /*null_count=*/-1);
      left_str_array = arrow::MakeArray(str_ad);
      left_batch = arrow::ExecBatch({left_fix_array, left_str_array}, n);
      (void)arrow::compute::ColumnArraysFromExecBatch(left_batch, &left_cols);
    }

    match.assign((static_cast<size_t>(n) + 7) / 8, 0);
    left_to_right.resize(static_cast<size_t>(n));
    std::iota(left_to_right.begin(), left_to_right.end(), 0);
    (void)stack.Init(pool, arrow::compute::KeyCompare::CompareColumnsToRowsTempStackUsage(n));
    ctx.hardware_flags = flags;
    ctx.stack = &stack;
  }

  void Run() override {
    std::fill(match.begin(), match.end(), 0);
    ctx.hardware_flags = flags;
    arrow::compute::KeyCompare::CompareColumnsToRows(
        static_cast<uint32_t>(fx->num_rows), nullptr, left_to_right.data(), &ctx, nullptr,
        nullptr, left_cols, row_table, /*are_cols_in_encoding_order=*/true, match.data());
    ConsumeBytes(match.data(), match.size());
  }
  // Fresh bytes of the left fixed-size key and of the left utf8 values.
  void Perturb() override {
    perturber.Walk(left_fix.data(), left_fix.size());
    perturber.Walk(left_str.data(), left_str.size());
  }
  uint64_t Checksum() override { return ChecksumBytes(match.data(), match.size()); }
  int64_t units() const override { return kRows; }
  double bytes_per_unit() const override { return 32.0; }
};

struct SwissEarlyFilterBench : Bench {
  std::unique_ptr<arrow::compute::SwissTable> table;
  std::vector<uint32_t> probes;
  std::vector<uint8_t> match_bitvector;
  std::vector<uint8_t> local_slots;
  int64_t flags;

  SwissEarlyFilterBench(uint64_t seed, int64_t hw) : flags(hw) {
    auto* pool = arrow::default_memory_pool();
    table = std::make_unique<arrow::compute::SwissTable>();
    (void)table->init(flags, pool, kSwissLogBlocks, /*no_hash_array=*/false);

    const int num_blocks = 1 << kSwissLogBlocks;
    std::vector<int> next_slot(static_cast<size_t>(num_blocks), 0);
    std::vector<uint32_t> hashes(static_cast<size_t>(kRows));
    {
      Rng r(seed);
      for (auto& h : hashes) h = r.Next32();
    }
    // Half-fill the table, then probe with a 50/50 mix of present and absent
    // hashes, which is what a hash-join minibatch sees.
    for (int64_t i = 0; i < kRows; ++i) {
      const uint32_t h = hashes[static_cast<size_t>(i)];
      const uint32_t b = arrow::compute::SwissTable::block_id_from_hash(h, kSwissLogBlocks);
      if (next_slot[b] >= 4) continue;
      const int slot = next_slot[b]++;
      const uint32_t gid = arrow::compute::SwissTable::global_slot_id(b, slot);
      table->insert_into_empty_slot(gid, h, gid);
    }

    probes.resize(static_cast<size_t>(kRows));
    for (int64_t i = 0; i < kRows; ++i) {
      probes[static_cast<size_t>(i)] = (i & 1) ? (hashes[static_cast<size_t>(i)] ^ 0xa5a5a5a5u)
                                               : hashes[static_cast<size_t>(i)];
    }
    match_bitvector.assign((static_cast<size_t>(kRows) + 7) / 8, 0);
    local_slots.assign(static_cast<size_t>(kRows), 0);
  }

  void Run() override {
    std::fill(match_bitvector.begin(), match_bitvector.end(), 0);
    std::fill(local_slots.begin(), local_slots.end(), 0);
    table->early_filter(static_cast<int>(probes.size()), probes.data(), match_bitvector.data(),
                        local_slots.data());
    ConsumeBytes(match_bitvector.data(), match_bitvector.size());
    ConsumeBytes(local_slots.data(), local_slots.size());
  }
  // Fresh bytes of the probe hashes.
  void Perturb() override {
    perturber.Walk(reinterpret_cast<uint8_t*>(probes.data()),
                   probes.size() * sizeof(uint32_t));
  }
  uint64_t Checksum() override {
    return ChecksumBytes(match_bitvector.data(), match_bitvector.size()) ^
           (ChecksumBytes(local_slots.data(), local_slots.size()) * 31ULL);
  }
  int64_t units() const override { return kRows; }
  double bytes_per_unit() const override { return 4.0 + 1.125 + 32.0 / 8.0; }
};

// --- row decode: EncoderBinary / EncoderVarBinary ---------------------------
// Unit: one decoded row.
//
// The row table RowTableEncoder produces is decoded back into columns by the
// same class: DecodeFixedLengthBuffers -> `EncoderBinary::Decode` (or
// `EncoderBinaryPair::Decode` when two integer columns are adjacent) and
// DecodeVaryingLengthBuffers -> `EncoderVarBinary::Decode`.  Both take a
// LightContext, and the AVX2 kernels of that decode path
// (compute/row/encode_internal_avx2.cc: EncoderBinary::DecodeHelper_avx2,
// EncoderBinaryPair::DecodeHelper_avx2, EncoderVarBinary::DecodeHelper_avx2)
// are selected inside them by `ctx->has_avx2()`, i.e. by the caller's
// `LightContext::hardware_flags` -- the same explicit-flags shape as the other
// row kernels, so both fnids below are dual-arm.
//
// Fixture: the shared RowFixture with a 16-byte key (see `fixed_width`).  Rows
// are encoded once with RowTableEncoder and every iteration decodes one column
// back out of the row table.  The varbinary destination offsets are computed at
// build time by the library's own `EncoderOffsets::Decode` from the same row
// table, so not one byte is copied from the source arrays; only
// `EncoderVarBinary::Decode` is timed.  Every destination buffer carries the
// extra 32 bytes the library's caller documents ("Output column varbinary
// buffer needs an extra 32B in avx2 version and 8B otherwise"), because the
// avx2 kernels copy whole 32-byte stripes.
struct RowDecodeFixture {
  static constexpr int64_t kFixedWidth = 16;
  std::unique_ptr<RowFixture> fx;
  arrow::compute::RowTableImpl row_table;
  std::vector<uint16_t> temp_u16;
  arrow::compute::KeyColumnArray temp_col;
  std::vector<uint8_t> fix_validity, fix_data;
  arrow::compute::KeyColumnArray fix_col;
  std::vector<uint8_t> var_validity, var_chars;
  std::vector<uint32_t> var_offsets;
  arrow::compute::KeyColumnArray var_col;
  arrow::util::TempVectorStack stack;
  arrow::compute::LightContext ctx;
  uint32_t fixed_offset_within_row = 0;
  size_t fix_bytes = 0, var_offset_count = 0, var_chars_bytes = 0;
  uint32_t var_total = 0;

  explicit RowDecodeFixture(uint64_t seed) {
    fx = std::make_unique<RowFixture>(seed, kFixedWidth);
    auto* pool = arrow::default_memory_pool();
    const int64_t n = fx->num_rows;

    arrow::compute::RowTableMetadata table_meta;
    table_meta.FromColumnMetadataVector(fx->metas, sizeof(uint64_t), sizeof(uint64_t));
    (void)row_table.Init(pool, table_meta);
    arrow::compute::RowTableEncoder encoder;
    encoder.Init(fx->metas, sizeof(uint64_t), sizeof(uint64_t));
    encoder.PrepareEncodeSelected(0, n, fx->cols);
    std::vector<uint16_t> row_ids(static_cast<size_t>(n));
    std::iota(row_ids.begin(), row_ids.end(), 0);
    (void)encoder.EncodeSelected(&row_table, static_cast<uint32_t>(n), row_ids.data());
    // The byte offset of the fixed-length column inside an encoded row: exactly
    // what the library passes to EncoderBinary::Decode.
    fixed_offset_within_row = table_meta.column_offsets[0];

    (void)stack.Init(pool, 1 << 20);
    ctx.hardware_flags = 0;
    ctx.stack = &stack;

    // Destination of the fixed-length decode: values only.  The null bitmap is
    // a separate sub-kernel (EncoderNulls::Decode) and the key column has no
    // nulls, so it is not part of this unit.
    fix_bytes = static_cast<size_t>(n) * static_cast<size_t>(kFixedWidth);
    fix_validity.assign((static_cast<size_t>(n) + 7) / 8, 0);
    fix_data.assign(fix_bytes + 64, 0);
    fix_col = arrow::compute::KeyColumnArray(
        arrow::compute::KeyColumnMetadata(true, kFixedWidth), n, fix_validity.data(),
        fix_data.data(), nullptr);
    temp_u16.assign(static_cast<size_t>(n), 0);
    temp_col = arrow::compute::KeyColumnArray(
        arrow::compute::KeyColumnMetadata(true, sizeof(uint16_t)), n, nullptr,
        reinterpret_cast<uint8_t*>(temp_u16.data()), nullptr);

    // Destination of the varbinary decode.  Its offsets buffer is filled from
    // the row table by the library's own EncoderOffsets::Decode, which is what
    // DecodeFixedLengthBuffers runs before DecodeVaryingLengthBuffers.
    var_chars_bytes = fx->str_data.size();
    var_offset_count = static_cast<size_t>(n) + 1;
    var_validity.assign((static_cast<size_t>(n) + 7) / 8, 0);
    var_offsets.assign(var_offset_count, 0);
    var_chars.assign(var_chars_bytes + 64, 0);
    var_col = arrow::compute::KeyColumnArray(
        arrow::compute::KeyColumnMetadata(false, sizeof(uint32_t)), n, var_validity.data(),
        reinterpret_cast<uint8_t*>(var_offsets.data()), var_chars.data());
    {
      std::vector<arrow::compute::KeyColumnArray> cols = {var_col};
      const std::vector<uint32_t> base_offsets = {0};
      arrow::compute::EncoderOffsets::Decode(0, static_cast<uint32_t>(n), row_table, &cols,
                                             base_offsets, &ctx);
    }
    // The total number of bytes the varbinary column occupies in the
    // destination buffer: the bound Perturb() draws its positions from.
    var_total = var_offsets[static_cast<size_t>(n)];

    // One decode of each column at build time.  It doubles as the fixture's
    // self-check: what comes back out of the row table must be what went in.
    // (The fixed column has no nulls and null strings are zero-length slices,
    // so both decodes are byte-exact against the source arrays.)
    arrow::compute::EncoderBinary::Decode(0, static_cast<uint32_t>(n),
                                          fixed_offset_within_row, row_table, &fix_col, &ctx,
                                          &temp_col);
    if (std::memcmp(fix_data.data(), fx->fix_data.data(), fix_bytes) != 0) {
      std::fprintf(stderr, "kbench: row decode fixture mismatch (fixed column)\n");
      std::abort();
    }
    arrow::compute::EncoderVarBinary::Decode(0, static_cast<uint32_t>(n), 0, row_table,
                                             &var_col, &ctx);
    for (int64_t i = 0; i <= n; ++i) {
      if (var_offsets[static_cast<size_t>(i)] !=
          static_cast<uint32_t>(fx->str_offsets[static_cast<size_t>(i)])) {
        std::fprintf(stderr, "kbench: row decode fixture mismatch (offsets at row %lld)\n",
                     static_cast<long long>(i));
        std::abort();
      }
    }
    if (std::memcmp(var_chars.data(), fx->str_data.data(), var_chars_bytes) != 0) {
      std::fprintf(stderr, "kbench: row decode fixture mismatch (varbinary column)\n");
      std::abort();
    }
  }
};

struct DecodeBinaryBench : Bench {
  std::unique_ptr<RowDecodeFixture> fx;
  int64_t flags;

  DecodeBinaryBench(uint64_t seed, int64_t hw) : flags(hw) {
    fx = std::make_unique<RowDecodeFixture>(seed);
  }
  void Run() override {
    fx->ctx.hardware_flags = flags;
    arrow::compute::EncoderBinary::Decode(0, static_cast<uint32_t>(fx->fx->num_rows),
                                          fx->fixed_offset_within_row, fx->row_table,
                                          &fx->fix_col, &fx->ctx, &fx->temp_col);
    ConsumeBytes(fx->fix_data.data(), fx->fix_bytes);
  }
  // The decode reads the encoded row table, whose length and offset fields are
  // structural; perturb one payload byte of three rows' key instead -- the bytes
  // this kernel copies, and nothing else, so no bounds are touched.
  void Perturb() override {
    const Perturber::Step s = perturber.Next(static_cast<size_t>(fx->fx->num_rows));
    const size_t mask = static_cast<size_t>(RowDecodeFixture::kFixedWidth) - 1;
    uint8_t* base = fx->row_table.mutable_var_length_rows() + fx->fixed_offset_within_row;
    base[fx->row_table.offsets()[s.a] + (s.va & mask)] ^= s.va;
    base[fx->row_table.offsets()[s.b] + (s.vb & mask)] ^= s.vb;
    base[fx->row_table.offsets()[s.c] + (s.vc & mask)] ^= s.vc;
    asm volatile("" ::: "memory");
  }
  uint64_t Checksum() override { return ChecksumBytes(fx->fix_data.data(), fx->fix_bytes); }
  int64_t units() const override { return kRows; }
  double bytes_per_unit() const override { return 2.0 * RowDecodeFixture::kFixedWidth; }
};

struct DecodeVarBinaryBench : Bench {
  std::unique_ptr<RowDecodeFixture> fx;
  int64_t flags;

  DecodeVarBinaryBench(uint64_t seed, int64_t hw) : flags(hw) {
    fx = std::make_unique<RowDecodeFixture>(seed);
  }
  void Run() override {
    fx->ctx.hardware_flags = flags;
    arrow::compute::EncoderVarBinary::Decode(0, static_cast<uint32_t>(fx->fx->num_rows), 0,
                                             fx->row_table, &fx->var_col, &fx->ctx);
    ConsumeBytes(fx->var_chars.data(), fx->var_chars_bytes);
    ConsumeBytes(fx->var_offsets.data(), fx->var_offset_count * sizeof(uint32_t));
  }
  // The decode reads the encoded row table and writes at the destination
  // offsets the fixture derived from it.  Three of those entries are moved to
  // positions drawn from a full-period sequence: every value stays inside
  // [0, total], so every write stays inside the destination buffer (whose
  // 64-byte tail is the slack the decoders already assume), and the bytes each
  // call copies -- one string per row, lengths from the row table -- are
  // unchanged.
  void Perturb() override {
    const Perturber::Step s =
        perturber.Next(static_cast<size_t>(fx->var_offset_count));
    fx->var_offsets[s.a] =
        static_cast<uint32_t>((static_cast<uint64_t>(fx->var_total) * s.va) / 256);
    fx->var_offsets[s.b] =
        static_cast<uint32_t>((static_cast<uint64_t>(fx->var_total) * s.vb) / 256);
    fx->var_offsets[s.c] =
        static_cast<uint32_t>((static_cast<uint64_t>(fx->var_total) * s.vc) / 256);
  }
  uint64_t Checksum() override {
    return ChecksumBytes(fx->var_chars.data(), fx->var_chars_bytes) * 31ULL ^
           ChecksumBytes(fx->var_offsets.data(), fx->var_offset_count * sizeof(uint32_t));
  }
  int64_t units() const override { return kRows; }
  double bytes_per_unit() const override {
    const double str = fx->fx->num_rows > 0
                           ? static_cast<double>(fx->var_chars_bytes) / fx->fx->num_rows
                           : 0.0;
    return 2.0 * (static_cast<double>(sizeof(uint32_t)) + str);
  }
};

// <<<KB-FAMILY: rowkernels
// >>>KB-FAMILY: swissjoin
// --- RowArray::DecodeSelected ----------------------------------------------
// Unit: one decoded value (whole-column decode of one material column).
//
// `RowArray::DecodeFixedLength/DecodeOffsets/DecodeVarLength/DecodeNulls` are
// private members (only RowArrayMerge is a friend), but `DecodeSelected` is
// *public* and is the library's own entry point into them: it selects the
// `*_avx2` sub-kernel when `hardware_flags_ & CpuInfo::AVX2` and the append
// starts on an 8-row-aligned output row, then finishes the remainder with the
// portable sub-kernel.  The arm is therefore an explicit `hardware_flags`
// argument, exactly like the other row kernels, and this measures the composite
// the way the join actually calls it.
struct DecodeSelectedBench : Bench {
  std::unique_ptr<RowFixture> fx;
  arrow::acero::RowArray rows;
  std::vector<uint16_t> row_ids_encode;
  std::vector<uint32_t> row_ids_decode;
  std::vector<arrow::compute::KeyColumnArray> temp_column_arrays;
  arrow::compute::ResizableArrayData out;
  int column_id;
  bool varlen;
  int64_t flags;
  size_t validity_bytes = 0, fixed_bytes = 0, var_bytes = 0;

  DecodeSelectedBench(uint64_t seed, int64_t hw, bool varlen_) : varlen(varlen_), flags(hw) {
    fx = std::make_unique<RowFixture>(seed);
    auto* pool = arrow::default_memory_pool();
    const int64_t n = fx->num_rows;
    column_id = varlen ? 1 : 0;

    row_ids_encode.resize(static_cast<size_t>(n));
    std::iota(row_ids_encode.begin(), row_ids_encode.end(), uint16_t{0});
    arrow::Status st = rows.AppendBatchSelection(pool, flags, fx->batch, 0, static_cast<int>(n),
                                          static_cast<int>(n), row_ids_encode.data(),
                                          temp_column_arrays);
    if (!st.ok()) std::abort();

    // Random access pattern, the way a hash-join probe decodes a payload.
    Rng r(seed ^ 0x5EED5EEDULL);
    row_ids_decode.resize(static_cast<size_t>(n));
    for (uint32_t& v : row_ids_decode) v = static_cast<uint32_t>(r.Below(n));

    const int log_num_rows_min =
        std::max(3, arrow::bit_util::Log2(static_cast<int64_t>(n)));
    st = out.Init(fx->batch[column_id].type(), pool, log_num_rows_min);
    if (!st.ok()) std::abort();

    // One full decode establishes the buffer layout and the exact number of
    // bytes each buffer holds; the row id selection is fixed, so every later
    // iteration writes the same sizes.
    st = rows.DecodeSelected(&out, column_id, static_cast<int>(n), row_ids_decode.data(),
                            pool);
    if (!st.ok()) std::abort();
    validity_bytes = (static_cast<size_t>(n) + 7) / 8;
    fixed_bytes = varlen ? (static_cast<size_t>(n) + 1) * 4
                         : static_cast<size_t>(n) * 8;
    var_bytes = varlen ? static_cast<size_t>(
                             out.mutable_data_as<int32_t>(
                                 arrow::compute::ResizableArrayData::kFixedLengthBuffer)[n])
                       : 0;
    out.Clear(/*release_buffers=*/false);
  }

  void Run() override {
    out.Clear(/*release_buffers=*/false);
    auto st = rows.DecodeSelected(&out, column_id, static_cast<int>(fx->num_rows),
                                 row_ids_decode.data(), arrow::default_memory_pool());
    (void)st;
    ConsumeBytes(out.mutable_data(arrow::compute::ResizableArrayData::kFixedLengthBuffer),
                 fixed_bytes);
    if (var_bytes > 0) {
      ConsumeBytes(out.mutable_data(arrow::compute::ResizableArrayData::kVariableLengthBuffer),
                   var_bytes);
    }
    ConsumeBytes(out.mutable_data(arrow::compute::ResizableArrayData::kValidityBuffer),
                 validity_bytes);
  }
  // The selection is the decode's input beyond the row data; move three entries
  // to different (always in-range) rows, which changes what is decoded and where.
  void Perturb() override {
    const Perturber::Step s = perturber.Next(row_ids_decode.size());
    row_ids_decode[s.a] = static_cast<uint32_t>(s.va % fx->num_rows);
    row_ids_decode[s.b] = static_cast<uint32_t>(s.vb % fx->num_rows);
    row_ids_decode[s.c] = static_cast<uint32_t>(s.vc % fx->num_rows);
  }
  uint64_t Checksum() override {
    // Re-decode once so the checksum reflects a full call, not a torn buffer.
    auto st = rows.DecodeSelected(&out, column_id, static_cast<int>(fx->num_rows),
                                 row_ids_decode.data(), arrow::default_memory_pool());
    if (!st.ok()) return 0;
    uint64_t h = ChecksumBytes(out.mutable_data(arrow::compute::ResizableArrayData::kFixedLengthBuffer),
                               fixed_bytes);
    h = h * 31 + ChecksumBytes(
                     out.mutable_data(arrow::compute::ResizableArrayData::kValidityBuffer),
                     validity_bytes);
    if (var_bytes > 0) {
      h = h * 31 +
          ChecksumBytes(
              out.mutable_data(arrow::compute::ResizableArrayData::kVariableLengthBuffer),
              var_bytes);
    }
    return h;
  }
  int64_t units() const override { return kRows; }
  double bytes_per_unit() const override {
    if (!varlen) return 8.0 + 0.125;
    const double str = fx->num_rows > 0
                           ? static_cast<double>(fx->str_data.size()) / fx->num_rows
                           : 0.0;
    return 4.0 + str + 0.125;
  }
};

// <<<KB-FAMILY: swissjoin
// >>>KB-FAMILY: anchor
// --- physics anchor --------------------------------------------------------
// Unit: one byte of a memcpy of `n` bytes (1 byte read + 1 byte written).
struct MemcpyBench : Bench {
  size_t n;
  std::vector<uint8_t> src, dst;
  explicit MemcpyBench(uint64_t seed, size_t bytes) : n(bytes) {
    Rng r(seed);
    src.resize(n);
    dst.resize(n, 0);
    for (size_t i = 0; i < n; ++i) src[i] = static_cast<uint8_t>(r.Next32());
  }
  void Run() override {
    std::memcpy(dst.data(), src.data(), n);
    ConsumeBytes(dst.data(), n);
  }
  uint64_t Checksum() override { return ChecksumBytes(dst.data(), n); }
  int64_t units() const override { return static_cast<int64_t>(n); }
  double bytes_per_unit() const override { return 2.0; }
};

// ---------------------------------------------------------------------------
// <<<KB-FAMILY: anchor
// fnid registry
// ---------------------------------------------------------------------------

enum FnKind {
  kDispatch = 0,  // internally dispatched: one line (installed arm)
  kDualArm = 1,   // explicit hardware_flags: <fnid>@scalar + <fnid>@simd
  kSingle = 2,    // one implementation, one line
  kDropped = 3,   // no reachable entry point
};

struct FnSpec {
  std::string id;
  FnKind kind;
  std::string drop_reason;
};

std::vector<FnSpec> Registry() {
  std::vector<FnSpec> v;
// >>>KB-FAMILY: bpacking
  const char* types[] = {"u8", "u16", "u32", "u64"};
  const int bits[] = {8, 16, 32, 64};
  for (int t = 0; t < 4; ++t) {
    for (int bw = 1; bw <= bits[t]; ++bw) {
      v.push_back({"bp." + std::string(types[t]) + "." + std::to_string(bw), kDispatch, ""});
    }
  }
// <<<KB-FAMILY: bpacking
  // >>>KB-FAMILY: bss
  for (int w : {2, 4, 8}) {
    v.push_back({"bss.enc." + std::to_string(w), kDispatch, ""});
    v.push_back({"bss.dec." + std::to_string(w), kDispatch, ""});
  }
  // <<<KB-FAMILY: bss
  // >>>KB-FAMILY: bss16
  // Width 16 has no AVX2 kernel at all (the dispatcher goes straight to the
  // scalar template), so bss.enc.16 / bss.dec.16 measure the dispatcher's
  // scalar path; they are kept here as an internal control on that path, and
  // are outside the scoped set.
  for (int w : {16}) {
    v.push_back({"bss.enc." + std::to_string(w), kDispatch, ""});
    v.push_back({"bss.dec." + std::to_string(w), kDispatch, ""});
  }
  // <<<KB-FAMILY: bss16
// >>>KB-FAMILY: levels
  v.push_back({"levels.defbitmap.rep0", kDispatch, ""});
  v.push_back({"levels.defbitmap.rep1", kDispatch, ""});
  v.push_back({"levels.gt", kDispatch, ""});
// <<<KB-FAMILY: levels
// >>>KB-FAMILY: bloomflt
  v.push_back({"bloom.sbbf.find", kDispatch, ""});
// <<<KB-FAMILY: bloomflt
// >>>KB-FAMILY: bloominsert
  v.push_back({"bloom.sbbf.insert", kSingle, ""});
// <<<KB-FAMILY: bloominsert
// >>>KB-FAMILY: rowkernels
  v.push_back({"row.hashbatch", kDualArm, ""});
  v.push_back({"row.hashmulticol", kDualArm, ""});
  v.push_back({"row.compare", kDualArm, ""});
  v.push_back({"row.swisstable.earlyfilter", kDualArm, ""});
  // The decode side of the row format: same row table, decoded back out.
  v.push_back({"row.decode.binary", kDualArm, ""});
  v.push_back({"row.decode.varbinary", kDualArm, ""});
// <<<KB-FAMILY: rowkernels
// >>>KB-FAMILY: swissjoin
  // RowArray's four decode sub-kernels are private members of
  // arrow::acero::RowArray (only RowArrayMerge is a friend) with no exported
  // entry point; the only way in is the public DecodeSelected(), which picks
  // the sub-kernels internally.  Reaching them individually would mean
  // re-implementing the class, so they are dropped rather than hacked around --
  // but the *stage* is measured through DecodeSelected itself, which is the
  // call the join makes and which selects the *_avx2 sub-kernels:
  v.push_back({"join.decode.selected.fixed", kDualArm, ""});
  v.push_back({"join.decode.selected.varlen", kDualArm, ""});
  for (const char* sub : {"fixedlen", "offsets", "varlen", "nulls"}) {
    v.push_back({"join.rowarray.decode." + std::string(sub), kDropped,
                 "private RowArray member; measured in composite through the public "
                 "join.decode.selected.* instead"});
  }
// <<<KB-FAMILY: swissjoin
// >>>KB-FAMILY: anchor
  for (int64_t bytes : {int64_t{1024}, int64_t{8192}, int64_t{65536}, int64_t{262144},
                        int64_t{1048576}}) {
    v.push_back({"anchor.memcpy." + std::to_string(bytes), kSingle, ""});
  }
// <<<KB-FAMILY: anchor
  return v;
}

const FnSpec* FindSpec(const std::vector<FnSpec>& reg, const std::string& id) {
  for (const FnSpec& s : reg) {
    if (s.id == id) return &s;
  }
  return nullptr;
}

std::unique_ptr<Bench> MakeBench(const std::string& id, uint64_t seed, int arm) {
  const uint64_t s = FnidSeed(seed, id);
  // The @simd arm is given the flag word the running library itself would use,
  // not a hardcoded ISA bit: these call sites take the flags from the caller, so
  // "the implementation the library installed" is by definition the one selected
  // by its own hardware_flags() (on a host without AVX2 that word has no AVX2 bit,
  // and crediting the AVX2 arm would be meaningless there).  @scalar stays 0.
// >>>KB-FAMILY: rowkernels swissjoin
  const int64_t hw =
      (arm == 0) ? 0 : arrow::internal::CpuInfo::GetInstance()->hardware_flags();
// <<<KB-FAMILY: rowkernels swissjoin

// >>>KB-FAMILY: bpacking
  if (id.rfind("bp.", 0) == 0) {
    // bp.<type>.<bit_width>
    const size_t dot = id.find('.', 3);
    if (dot == std::string::npos) return nullptr;
    const std::string type = id.substr(3, dot - 3);
    const int bw = std::atoi(id.c_str() + dot + 1);
    if (type == "u8") return std::make_unique<BpBench<uint8_t>>(s, bw);
    if (type == "u16") return std::make_unique<BpBench<uint16_t>>(s, bw);
    if (type == "u32") return std::make_unique<BpBench<uint32_t>>(s, bw);
    if (type == "u64") return std::make_unique<BpBench<uint64_t>>(s, bw);
    return nullptr;
  }
// <<<KB-FAMILY: bpacking
// >>>KB-FAMILY: bss
  if (id.rfind("bss.", 0) == 0) {
    const bool enc = id.rfind("bss.enc.", 0) == 0;
    const bool dec = id.rfind("bss.dec.", 0) == 0;
    if (!enc && !dec) return nullptr;
    const int w = std::atoi(id.c_str() + 8);
    return std::make_unique<BssBench>(s, w, enc);
  }
// <<<KB-FAMILY: bss
// >>>KB-FAMILY: levels
  if (id == "levels.defbitmap.rep0") return std::make_unique<DefBitmapBench>(s, false);
  if (id == "levels.defbitmap.rep1") return std::make_unique<DefBitmapBench>(s, true);
  if (id == "levels.gt") return std::make_unique<GreaterThanBench>(s, /*rhs=*/1);
// <<<KB-FAMILY: levels
// >>>KB-FAMILY: bloomflt
  if (id == "bloom.sbbf.find") return std::make_unique<SbbfFindBench>(s);
// <<<KB-FAMILY: bloomflt
// >>>KB-FAMILY: bloominsert
  if (id == "bloom.sbbf.insert") return std::make_unique<SbbfInsertBench>(s);
// <<<KB-FAMILY: bloominsert
// >>>KB-FAMILY: rowkernels
  if (id == "row.hashbatch") return std::make_unique<HashBatchBench>(s, hw);
  if (id == "row.hashmulticol") return std::make_unique<HashMultiColumnBench>(s, hw);
  if (id == "row.compare") return std::make_unique<CompareBench>(s, hw);
  if (id == "row.swisstable.earlyfilter") return std::make_unique<SwissEarlyFilterBench>(s, hw);
  if (id == "row.decode.binary") return std::make_unique<DecodeBinaryBench>(s, hw);
  if (id == "row.decode.varbinary") return std::make_unique<DecodeVarBinaryBench>(s, hw);
// <<<KB-FAMILY: rowkernels
// >>>KB-FAMILY: swissjoin
  if (id == "join.decode.selected.fixed") return std::make_unique<DecodeSelectedBench>(s, hw, false);
  if (id == "join.decode.selected.varlen") return std::make_unique<DecodeSelectedBench>(s, hw, true);
// <<<KB-FAMILY: swissjoin
// >>>KB-FAMILY: anchor
  if (id.rfind("anchor.memcpy.", 0) == 0) {
    const long long bytes = std::atoll(id.c_str() + 14);
    if (bytes <= 0) return nullptr;
    return std::make_unique<MemcpyBench>(s, static_cast<size_t>(bytes));
  }
// <<<KB-FAMILY: anchor
  return nullptr;
}

// ---------------------------------------------------------------------------
// timing
// ---------------------------------------------------------------------------

using Clock = std::chrono::steady_clock;

double Ns(const Clock::time_point& a, const Clock::time_point& b) {
  return std::chrono::duration<double, std::nano>(b - a).count();
}

double MeasureSampleNs(Bench& b, int64_t reps) {
  const Clock::time_point t0 = Clock::now();
  for (int64_t i = 0; i < reps; ++i) {
    b.Perturb();  // fresh input for every timed call: see Perturber
    b.Run();
  }
  const Clock::time_point t1 = Clock::now();
  return Ns(t0, t1);
}

struct Timing {
  double ns_per_unit = 0;
  double lo = 0, hi = 0;
  int n = 0;
  int64_t reps = 0;
  std::vector<double> samples;  // sorted, ns/unit; printed by the `samples` mode
};

// Times one or more benches with their samples round-robined.  With two arms in
// the list (a scalar/SIMD pair selected by an explicit `hardware_flags`
// argument) both arms sample the *same* distribution of external interference,
// so the ratio is far less sensitive to a burst of contention landing on one
// arm's window than two sequential measurements would be.  With one arm it is a
// plain sequential measurement.
void TimeArms(const std::vector<Bench*>& benches, std::vector<Timing>* out) {
  const size_t n_arms = benches.size();
  out->assign(n_arms, Timing{});
  std::vector<int64_t> reps(n_arms, 1);
  std::vector<std::vector<double>> samples(n_arms);
  std::vector<int> recalibrations(n_arms, 0);

  for (size_t i = 0; i < n_arms; ++i) {
    Bench& b = *benches[i];
    b.Run();  // warm-up: resolves dispatch, faults in pages, primes caches
    double one = MeasureSampleNs(b, 1);
    if (!(one > 0)) one = 1e3;
    reps[i] = static_cast<int64_t>(std::ceil(kTargetSampleNs / one)) + 1;
    if (reps[i] < 1) reps[i] = 1;
  }

  bool progress = true;
  while (progress) {
    progress = false;
    for (size_t i = 0; i < n_arms; ++i) {
      if (static_cast<int>(samples[i].size()) >= kSamples) continue;
      Bench& b = *benches[i];
      const double total = MeasureSampleNs(b, reps[i]);
      if (total < kTargetSampleNs * 0.9 && recalibrations[i] < kMaxRecalibrations) {
        ++recalibrations[i];
        const double factor = kTargetSampleNs / std::max(total, 1.0) * 1.05;
        int64_t next = static_cast<int64_t>(static_cast<double>(reps[i]) * factor) + 1;
        if (next <= reps[i]) next = reps[i] + 1;
        reps[i] = std::min<int64_t>(next, int64_t{1} << 42);
        progress = true;
        continue;
      }
      samples[i].push_back(total / static_cast<double>(reps[i]) /
                           static_cast<double>(b.units()));
      progress = true;
    }
  }

  for (size_t i = 0; i < n_arms; ++i) {
    std::sort(samples[i].begin(), samples[i].end());
    Timing& t = (*out)[i];
    t.n = static_cast<int>(samples[i].size());
    t.lo = samples[i].front();
    t.hi = samples[i].back();
    t.ns_per_unit = samples[i][samples[i].size() / 2];
    t.reps = reps[i];
    t.samples = samples[i];
  }
}

// >>>KB-FAMILY: anchor
Timing TimeBench(Bench& b) {
  std::vector<Timing> t;
  TimeArms({&b}, &t);
  return t[0];
}
// <<<KB-FAMILY: anchor

// ---------------------------------------------------------------------------
// physics anchor
// ---------------------------------------------------------------------------

// >>>KB-FAMILY: anchor
double MemcpyBytesPerNs(uint64_t seed, size_t bytes, bool verbose) {
  MemcpyBench b(seed, bytes);
  Timing t = TimeBench(b);
  // t.ns_per_unit is ns per byte copied; the memory traffic is 2 bytes
  // (1 read + 1 written) per byte copied.
  const double rate = 2.0 / t.ns_per_unit;
  if (verbose) {
    fprintf(stderr, "# anchor memcpy bytes=%zu ns_per_byte=%.6f traffic_bytes_per_ns=%.3f\n",
            bytes, t.ns_per_unit, rate);
  }
  return rate;
}
// <<<KB-FAMILY: anchor

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

bool EnvForcesScalarDispatch() {
  const char* v = std::getenv("ARROW_USER_SIMD_LEVEL");
  if (v == nullptr) return false;
  std::string s(v);
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s == "NONE";
}

void Usage() {
  fprintf(stderr,
          "usage: kbench dump|bench|samples <seed> <fnid> [<fnid> ...]\n"
          "       <seed> is a 64-bit unsigned decimal\n"
          "       <fnid> may be 'all' to select every scoped function\n"
          "       dump    -> '<label> <hex64>'\n"
          "       bench   -> '<label> <ns_per_unit> <hex64>'\n"
          "       samples -> '<label> <n> <reps> <sorted ns_per_unit samples...>'\n");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    Usage();
    return 2;
  }
  const std::string mode = argv[1];
  if (mode != "dump" && mode != "bench" && mode != "samples") {
    Usage();
    return 2;
  }
  char* end = nullptr;
  const uint64_t seed = std::strtoull(argv[2], &end, 10);
  if (end == argv[2] || *end != '\0') {
    fprintf(stderr, "kbench: bad seed '%s'\n", argv[2]);
    return 2;
  }

  const std::vector<FnSpec> registry = Registry();
  const bool scalar_env = EnvForcesScalarDispatch();

  // Host context, on stderr so the measurement lines stay machine-parseable.
  {
    const auto* cpu = arrow::internal::CpuInfo::GetInstance();
    fprintf(stderr,
            "# host model=\"%.*s\" vendor=%d sse4_2=%d avx2=%d avx512=%d bmi2=%d "
            "efficient_bmi2=%d hardware_flags=0x%llx user_simd=%s dispatch=%s\n",
            static_cast<int>(cpu->model_name().size()), cpu->model_name().data(),
            static_cast<int>(cpu->vendor()), cpu->IsSupported(cpu->SSE4_2) ? 1 : 0,
            cpu->IsSupported(cpu->AVX2) ? 1 : 0, cpu->IsSupported(cpu->AVX512) ? 1 : 0,
            cpu->IsSupported(cpu->BMI2) ? 1 : 0, cpu->HasEfficientBmi2() ? 1 : 0,
            static_cast<unsigned long long>(cpu->hardware_flags()),
            scalar_env ? "NONE" : "(unset)", scalar_env ? "scalar" : "installed");
    fflush(stderr);
  }

  std::vector<std::string> ids;
  for (int i = 3; i < argc; ++i) {
    if (std::strcmp(argv[i], "all") == 0) {
      for (const FnSpec& s : registry) {
        if (s.kind != kDropped) ids.push_back(s.id);
      }
    } else {
      ids.push_back(argv[i]);
    }
  }

// >>>KB-FAMILY: anchor
  struct Measured {
    std::string label;
    double ns_per_unit;
    double lo, hi;
    int n;
    double bytes_per_unit;
  };
// <<<KB-FAMILY: anchor
// >>>KB-FAMILY: anchor
  std::vector<Measured> measured;
// <<<KB-FAMILY: anchor

  for (const std::string& id : ids) {
    const FnSpec* spec = FindSpec(registry, id);
    if (spec == nullptr) {
      fprintf(stderr, "# unknown fnid: %s (skipped)\n", id.c_str());
      continue;
    }
    if (spec->kind == kDropped) {
      fprintf(stderr, "# dropped %s: %s\n", id.c_str(), spec->drop_reason.c_str());
      continue;
    }

    struct ArmRun {
      int arm;
      const char* suffix;
    };
    std::vector<ArmRun> arms;
    if (spec->kind == kDualArm) {
      arms = {{1, "@simd"}, {0, "@scalar"}};
    } else if (spec->kind == kDispatch && scalar_env) {
      arms = {{1, "@scalar"}};  // the dispatcher itself resolved to the scalar arm
    } else {
      arms = {{1, ""}};
    }

    // Build every arm of this fnid up front so that a scalar/SIMD pair can be
    // timed with interleaved samples (see TimeArms).
    std::vector<std::string> labels;
    std::vector<std::unique_ptr<Bench>> benches;
    for (const ArmRun& a : arms) {
      std::unique_ptr<Bench> bench = MakeBench(id, seed, a.arm);
      if (bench == nullptr) {
        fprintf(stderr, "# FAILED to construct %s%s (skipped)\n", id.c_str(), a.suffix);
        continue;
      }
      labels.push_back(id + a.suffix);
      benches.push_back(std::move(bench));
    }
    if (benches.empty()) continue;

    if (mode == "dump") {
      for (size_t k = 0; k < benches.size(); ++k) {
        // Several calls with fresh input, checksumming the last: a kernel that
        // memoises on its input, or on the buffer address, answers the later
        // calls with the first call's result -- and that stale result is then
        // compared, byte for byte, with the base tree's, which recomputed.
        for (int64_t j = 0; j < kDumpCalls; ++j) {
          benches[k]->Perturb();
          benches[k]->Run();
        }
        printf("%s %016llx\n", labels[k].c_str(),
               static_cast<unsigned long long>(benches[k]->Checksum()));
        fflush(stdout);
      }
      continue;
    }

    std::vector<Bench*> raw;
    for (auto& b : benches) raw.push_back(b.get());
    std::vector<Timing> timings;
    TimeArms(raw, &timings);
    for (size_t k = 0; k < benches.size(); ++k) {
      const uint64_t cs = benches[k]->Checksum();
      if (mode == "samples") {
        // Every raw sample, sorted, for offline ratio statistics.
        printf("samples %s %d %lld", labels[k].c_str(), timings[k].n,
               (long long)timings[k].reps);
        for (double v : timings[k].samples) printf(" %.6f", v);
        printf("\n");
      } else {
        printf("%s %.6f %016llx\n", labels[k].c_str(), timings[k].ns_per_unit,
               static_cast<unsigned long long>(cs));
      }
      fflush(stdout);
      fprintf(stderr, "# spread %s min=%.6f med=%.6f max=%.6f n=%d reps=%lld\n",
              labels[k].c_str(), timings[k].lo, timings[k].ns_per_unit, timings[k].hi,
              timings[k].n, (long long)timings[k].reps);
// >>>KB-FAMILY: anchor
      measured.push_back({labels[k], timings[k].ns_per_unit, timings[k].lo, timings[k].hi,
                          timings[k].n, benches[k]->bytes_per_unit()});
// <<<KB-FAMILY: anchor
    }
  }

// >>>KB-FAMILY: anchor
  if (!measured.empty()) {
    // Both measurement modes: `samples` is the one the measurement reads, and the
    // physics anchor is the harness's own detector for a kernel that reports
    // less work than the memory system can physically do -- which is what a
    // cache-hit or a hoisted call looks like.  The verdicts go to stderr, so
    // the machine-readable stdout is unchanged.
    // Physics anchor: max achievable memory-traffic rate on this machine, at a
    // working-set size comparable to what the kernels touch.
    double best_rate = 0.0;
    for (size_t bytes : {size_t{8192}, size_t{65536}, size_t{262144}}) {
      best_rate = std::max(best_rate, MemcpyBytesPerNs(seed, bytes, true));
    }
    for (const Measured& m : measured) {
      if (m.bytes_per_unit <= 0) continue;
      const double implied = m.bytes_per_unit / m.ns_per_unit;
      if (implied > best_rate * 1.5) {
        fprintf(stderr,
                "# PHYSICS-VIOLATION %s implied=%.3f bytes_per_ns anchor=%.3f ratio=%.2f "
                "(suspect hoisting or a wrong bytes-per-unit estimate)\n",
                m.label.c_str(), implied, best_rate, implied / best_rate);
      } else {
        fprintf(stderr, "# physics ok %s implied=%.3f bytes_per_ns anchor=%.3f ratio=%.2f\n",
                m.label.c_str(), implied, best_rate, implied / best_rate);
      }
    }
  }
// <<<KB-FAMILY: anchor
  return 0;
}
