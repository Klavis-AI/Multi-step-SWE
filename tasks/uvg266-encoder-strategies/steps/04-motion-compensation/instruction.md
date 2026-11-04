# Step 4 — Motion compensation

## 1. Implement this milestone

`/app/uvg266` is the uvg266 VVC encoder at a pinned upstream commit with the
optimized implementation of one or more strategy families removed. The encoder
is complete and correct: it builds, passes its own test suite, and produces
valid bitstreams using the portable C implementations in
`src/strategies/generic/`. It is simply slow where the optimized code is
missing.

**This milestone: make the subpel interpolation filters faster, without changing a single output byte.**

The family is `src/strategies/generic/ipol-generic.c`, and the functions you are
asked for are:

- `uvg_filter_hpel_blocks_hor_ver_luma`, `uvg_filter_hpel_blocks_diag_luma`,
  `uvg_filter_qpel_blocks_hor_ver_luma`, `uvg_filter_qpel_blocks_diag_luma` —
  the fractional-pel luma block filters
- `uvg_sample_quarterpel_luma`, `uvg_sample_quarterpel_luma_hi` — the 8-tap
  luma FIR, to pixels and to intermediate precision
- `uvg_sample_octpel_chroma`, `uvg_sample_octpel_chroma_hi` — the 4-tap chroma
  FIR

All of them are currently served by `src/strategies/generic/ipol-generic.c`. The
optimized twins upstream ships for other instruction sets are not in this tree,
so `src/strategies/strategies-ipol.c` registers only the generic implementation
and the encoder runs the C path for every one of them.

Optimize them however you judge best — C with intrinsics, hand-written
assembly, or a better portable C implementation. You are measured on the
function the encoder actually installs for each one, so any route that makes it
faster is acceptable. If you add files, register them in
`src/strategies/strategies-ipol.c` and add them to the `if(UVG_HAVE_X86_64_AVX2)`
source list in `src/CMakeLists.txt`, which is the list that applies the
project's AVX2 flags (`-mavx2 -mbmi -mpopcnt -mlzcnt -mbmi2`, from
`cmake/cpu.cmake`). A file left in the generic list is compiled without them.

**What success means here.**

1. **The encoder must behave exactly as it does now.** This is an optimization
   task, not a behaviour-change task: for every input and every configuration
   the encoded bitstream must be byte-for-byte what this tree produces today —
   not merely for the cases you happen to test. The interpolation weights are integers and the results are
clipped, so this is bit-exact by construction; a reordering that changes the
clipping, or a phase table read differently, is a failure, however fast it is. Do not edit the
   generic implementations under `src/strategies/generic/`: they define the
   correct output.
2. **These functions must get faster**, measured individually by calling the
   implementation the encoder installs for each. Only these are in scope;
   optimizing some other part of the encoder does nothing for this milestone.

`/app/BENCHMARK.md` gives the exact commands for both — the behaviour checks
and the timing — so you can confirm where you stand before finishing. The same
checks are re-run against a fresh workload afterwards.

**The workload is not disclosed.** It is generated from a seed you are not given, and the block shapes and sizes it draws from are described
in `/app/BENCHMARK.md` but not the draw itself. Optimize for the general case:
winning only on one block size or one input distribution is not enough.

**Runtime dispatch.** `uvg_strategyselector_init()` chooses, per function, the
highest-priority registered implementation and stores it in a global pointer.
Nothing else in the encoder calls the kernels directly, so a faster
implementation reaches the pipeline only once it is registered. The timed run drives `--cpuid`/`--no-cpuid` through that same selector.

## 2. Read these local references

- `src/strategies/strategies-ipol.c` — the dispatcher and the registration
  function. Register new implementations here.
- `src/strategies/strategies-ipol.h` — the exact signatures, the
  `ipol_blocks_func` and `uvg_sample_*_func` typedefs, and the scratch-array
  sizes the block filters require.
- `src/strategies/generic/ipol-generic.c` — the C implementation this replaces,
  and the reference for what is correct.
- `src/filter.c` — `uvg_g_luma_filter` and `uvg_g_chroma_filter`, the phase
  tables the sample filters index by the motion vector's low bits.
- `src/search_inter.c` — how the encoder calls these and how it pads the source
  block.
- `tests/inter_recon_bipred_tests.c` — present but commented out of the build
  upstream, so treat it as documentation rather than a gate.

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
