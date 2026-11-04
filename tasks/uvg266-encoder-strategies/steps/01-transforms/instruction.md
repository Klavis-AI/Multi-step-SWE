# Step 1 — the VVC transforms

## 1. Implement this milestone

`/app/uvg266` is the uvg266 VVC encoder at a pinned upstream commit with the
optimized implementation of one or more strategy families removed. The encoder
is complete and correct: it builds, passes its own test suite, and produces
valid bitstreams using the portable C implementations in
`src/strategies/generic/`. It is simply slow where the optimized code is
missing.

**This milestone: make the transforms faster, without changing a single output
byte.**

It covers the whole transform family — the forward and inverse transforms that
turn a residual block into coefficients and back:

- `uvg_dct_4x4`, `uvg_dct_8x8`, `uvg_dct_16x16`, `uvg_dct_32x32`
- `uvg_idct_4x4`, `uvg_idct_8x8`, `uvg_idct_16x16`, `uvg_idct_32x32`
- `uvg_mts_dct`, `uvg_mts_idct` — the multiple-transform-selection entry points,
  which pick between DCT-II, DCT-VIII and DST-VII by block shape and
  `mts_type`.

Every one of these is currently served by `src/strategies/generic/dct-generic.c`.
The optimized twins that upstream ships for other instruction sets are not in
this tree, so `src/strategies/strategies-dct.c` registers only the generic
implementation and the encoder runs the C path for all ten.

Optimize them however you judge best — C with intrinsics, hand-written
assembly, or a better portable C implementation. You are measured on the
function the encoder actually installs for each one, so any route that makes it
faster is acceptable. If you add files, register them in
`src/strategies/strategies-dct.c` and add them to the `if(UVG_HAVE_X86_64_AVX2)`
source list in `src/CMakeLists.txt`, which is the list that applies the
project's AVX2 flags (`-mavx2 -mbmi -mpopcnt -mlzcnt -mbmi2`, from
`cmake/cpu.cmake`). A file left in the generic list is compiled without them.

**What success means here.**

1. **The encoder must behave exactly as it does now.** This is an optimization
   task, not a behaviour-change task: for every input and every configuration
   the encoded bitstream must be byte-for-byte what this tree produces today —
   not merely for the cases you happen to test. A transform that rounds
   differently, saturates differently, or is right "almost always" is a
   failure, however fast it is. Do not edit the generic implementations under
   `src/strategies/generic/`: they define the correct output.
2. **These functions must get faster**, measured individually by calling the
   implementation the encoder installs for each. Only these are in scope;
   optimizing some other part of the encoder does nothing for this milestone.

`/app/BENCHMARK.md` gives the exact commands for both — the behaviour checks
and the timing — so you can confirm where you stand before finishing. The same
checks are re-run against a fresh workload afterwards.

**The workload is not disclosed.** It is generated from a seed you are not given, and the block shapes and sizes it draws from are described
in `/app/BENCHMARK.md` but not the draw itself. Optimize the transforms for the
general case: winning only on one block size or one input distribution is not
enough.

**Runtime dispatch.** `uvg_strategyselector_init()` chooses, per function, the
highest-priority registered implementation and stores it in a global pointer.
Nothing else in the encoder calls the kernels directly, so a faster
implementation reaches the pipeline only once it is registered. The timed run drives `--cpuid`/`--no-cpuid` through that same selector.

## 2. Read these local references

**Normative references, all local:**

- `src/strategies/strategies-dct.c` — the family's dispatcher: the global
  function pointers, the registration function, and the width/height selection
  helpers. Register new implementations here.
- `src/strategies/strategies-dct.h` — the exact signatures, the `STRATEGIES_DCT_EXPORTS`
  list, and the `dct_func` / `mts_dct_func` / `mts_idct_func` typedefs.
- `src/strategies/generic/dct-generic.c` — the C implementation this replaces.
  It is the reference for what is correct.
- `src/transform.c` — how the encoder calls these and with what block sizes,
  which `mts_type` values arrive, and what the `cu_info_t` argument carries.
- `src/strategies/generic/mip_data_generic.h`, `src/uvg266.h` — shared tables and
  the `uvg_mts` / `color_t` / `cu_info_t` enums.
- `tests/dct_tests.c`, `tests/mts_tests.c` — the suite's own differential tests
  for this family, which check every registered strategy against the generic
  result.
- `tests/speed_tests.c` — upstream's per-strategy speed test, which times every
  registered `dct_*`/`idct_*` strategy.
- `/app/BENCHMARK.md` — the exact behaviour and timing commands, and how the
  in-scope set is enumerated.
- `/app/tools/bench.c` — the practice harness. Compile it against your build and
  it times each function the encoder installs, the same way the timed run
  does.
- `/app/WORKSPACE_HANDOFF.md` — what earlier milestones changed, and how to
  build and measure locally.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace. Keep `/app/uvg266` as the source
  tree, keep the build system working
  (`cmake -S /app/uvg266 -B /app/build -DCMAKE_BUILD_TYPE=Release`), and
  preserve the handoff documents.
- **This milestone is measured on AVX2 or narrower.** The upstream reference for
  this family is 256-bit, so you may use 256-bit vectors. The target host
  provides SSE2 and AVX2; it has no AVX-512. Do not raise the build's
  instruction-set level above what `src/CMakeLists.txt` already configures for
  the AVX2 strategy files.
- Earlier milestones must keep working. This workspace is cumulative: what you
  optimized in an earlier step is still here and is still part of this step's
  baseline, so a regression breaks earlier behaviour as well as this step.
- The public network is available for official documentation and
  ordinary dependency lookup. **Do not fetch, copy, vendor, or transcribe
  uvg266's or any other project's optimized implementation of this family from
  the internet or a package mirror.** The optimization must be yours. Do not
  inspect `/tests`, later-step directories, or solution material.
- Do not weaken, filter, rename, or disable tests.
- Leave a reproducible handoff in `/app/WORKSPACE_HANDOFF.md`: what you changed,
  the speedup you measured locally, and how to rebuild.
