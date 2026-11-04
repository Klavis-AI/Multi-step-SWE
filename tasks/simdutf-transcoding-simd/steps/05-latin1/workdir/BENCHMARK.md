# How to check your work

## Build

Two build directories: `/app/build` carries the project's own
test suite, `/app/tools/bld` is the tree the timer links against. Building the
timer into `/app/build` would reconfigure that directory with the tests off and
silently turn the behaviour check below into a no-op.

```bash
# the project's suite, for the behaviour check
cmake -S /app/simdutf -B /app/build -DCMAKE_BUILD_TYPE=Release -DSIMDUTF_TESTS=ON
cmake --build /app/build -j"$(nproc)"

# the timer, re-run after every rebuild of the library -- it links it, so a
# stale binary measures the tree as it used to be
bash /app/tools/build-measure.sh
```

## Behaviour must not change

The library has to produce exactly the bytes it produced before. All of these
are re-run, on inputs you have not seen, when your work is checked.

1. **Upstream's own suite** must gain no new failure: `ctest --test-dir /app/build`
   (the tree you inherited passes all of it; only *new* failures matter).
2. **The end-to-end digest** of the whole transcoding contract must be
   unchanged. The checker drives a fixed battery of fifty operations — every
   family the library exposes, in selected valid, error-reporting, replacement
   and endianness forms — through the public API, and compares a digest of
   each one's output.
   It samples the contract rather than enumerating it: the suite above is what
   covers the operations it leaves out. `/app/tools/` does not ship that
   program; the per-function timer below covers the same functions individually.
3. **The functions in this milestone** must produce byte-identical output to
   the versions you inherited — and, where the function returns
   `simdutf::result`, the same `error` code and `count` — checked directly
   rather than through the whole codec.

Two further gates, both applied to your changes:

- **The AVX2 envelope**: no AVX-512 construct in the sources that build the
  AVX2 path. The host has no AVX-512, so such code cannot run here anyway.
- **An end-to-end non-regression guard**: the full transcoding pipeline must not
  be more than 50% slower than the base tree every milestone is measured
  against — what milestone 1 started from, not your latest checkpoint.

## The timed set

The functions below are the whole of this milestone's scope. Each is timed by
calling the implementation the library actually installed for it — not by timing
a whole conversion — so a faster function shows up at its full value instead of
being diluted, and every one of them is timed, so none of the work goes
unnoticed. **A function left untouched measures at 1.0x**, so "done" means
"every line below is faster".

| function | timed on |
| --- | --- |
| `convert_latin1_to_utf8` | `lat` |
| `convert_utf8_to_latin1` | `u8lat` |
| `convert_utf8_to_latin1_with_errors` | `u8lat` |
| `convert_valid_utf8_to_latin1` | `u8lat` |
| `convert_utf16le_to_latin1_with_errors` | `u16lat` |
| `convert_utf32_to_latin1_with_errors` | `u32lat` |
| `convert_valid_utf32_to_latin1` | `u32lat` |
| `latin1_length_from_utf8` | `u8lat` |
| `utf8_length_from_latin1` | `lat` |

Each function is timed on **one call over a corpus drawn for that repetition**,
in its own process, and the reported figure is the median of nine such draws — a
different draw every repetition, because timing the same buffer repeatedly would be
meaningless: a result computed once and reused is not a faster conversion.

**Read your own number as an upper bound.** `/app/tools/measure <fn> <file>
bench N` re-times the same buffer, so it reports a warm, page-faulted-in,
branch-predictor-trained figure. The checker gets one cold call on input this
container has never seen. On the sample corpus the two differ by roughly 1.5x on
the same function, so a local ratio of, say, 5x against the tree you inherited
may be closer to 3x where it counts. The harness's `bench1` mode is exactly what
the checker does — one call, no warm-up — and is worth running on a few different
files before you conclude you are done.

**In this milestone's file set but not timed** — measured on the
corpus, GCC already vectorizes the portable widening loops and the narrowings
are at parity, so the ratio has nothing to resolve. They still have to keep
working, and the end-to-end gate covers them:

| function | portable / optimized |
| --- | ---: |
| `convert_latin1_to_utf16le` | 1.33x |
| `convert_latin1_to_utf32` | 0.98x |
| `convert_utf16le_to_latin1` | 1.07x |
| `convert_valid_utf16le_to_latin1` | 0.74x |
| `convert_utf32_to_latin1` | 0.96x |
| `convert_latin1_to_utf16be` | *the big-endian mirror of the row above it* |
| `convert_utf16be_to_latin1` | *the big-endian mirror of the row above it* |
| `convert_utf16be_to_latin1_with_errors` | *the big-endian mirror of the UTF-16LE form, which is timed* |
| `convert_valid_utf16be_to_latin1` | *the big-endian mirror of the row above it* |

The big-endian forms are not separately timed: they are the same kernel over a
byte-swapped corpus, so they carry the same ratio, and timing both would just
double-count one piece of work. They still have to keep working.

## How to time it yourself

```bash
/app/tools/measure <function> /app/workload/<file> bench 7
```

prints nanoseconds per call, the median of 7 repetitions, lower is better. Run
it on an otherwise idle machine and pin it (`taskset -c 0,1`) — the checker
container pins to two cores and yours will be noisier without it. The tree you
inherited is the denominator: to see a ratio, measure the same function on the
same file before and after your change, or keep a copy of the original tree.

**The corpus this runs on is not the corpus the checker uses.** `/app/workload`
is one draw from the distribution described below; the checker draws its own,
inside its own container, from a seed you never see. Treat your own figures as
indicative.

## The corpus distribution

`/app/tools/make_corpus.py <dir> <seed>` regenerates a corpus from any seed and
is the same generator the checker uses — so you can make yourself as many draws
from the distribution as you want, and the more of them your kernel is measured
on the better. What it produces:


| key | file | what it is |
| --- | --- | --- |
| `u8` | `utf8.txt` | mixed-script UTF-8: long ASCII runs, 2-byte Latin/Greek/Cyrillic, 3-byte CJK, 4-byte emoji and astral planes |
| `u8err` | `utf8err.txt` | the same text with invalid sequences injected — stray continuation bytes, overlong forms, encoded surrogates, truncated sequences |
| `u8lat` | `utf8latin1.txt` | the UTF-8 encoding of a Latin-1 document, so the Latin-1 conversions run to completion |
| `u16le` / `u16be` | `utf16le.bin` / `utf16be.bin` | the same mixed-script text as UTF-16, little- and big-endian |
| `u16bad` | `utf16bad.bin` | UTF-16 carrying lone surrogates for the repair pass |
| `u32le` | `utf32le.bin` | the same text as UTF-32 |
| `u16lat` / `u32lat` | `utf16latin1.bin` / `utf32latin1.bin` | the Latin-1 document as UTF-16 and UTF-32 |
| `lat` | `latin1.bin` | the Latin-1 document itself |
| `ascii` | `ascii.txt` | pure ASCII |
| `bin` | `bin.bin` | uniformly random bytes, for base64 encode |
| `b64` | `base64.txt` | that binary as RFC 4648 base64 with line breaks |
| `ndl` | `findneedle.txt` | ASCII in which the searched-for byte does not occur, so the search scans the whole buffer |


Every file is a deterministic function of the seed.
