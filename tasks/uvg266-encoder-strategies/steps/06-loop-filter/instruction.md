# Step 6 — Sample adaptive offset

## 1. Implement this milestone

`/app/uvg266` is the uvg266 VVC encoder at a pinned upstream commit with the
optimized implementation of one or more strategy families removed. The encoder
is complete and correct: it builds, passes its own test suite, and produces
valid bitstreams using the portable C implementations in
`src/strategies/generic/`. It is simply slow where the optimized code is
missing.

**This milestone: make sample adaptive offset faster, without changing a single output byte.**

The family is `src/strategies/generic/sao-generic.c` (with the band-offset
kernel shared from `sao_shared_generics.h`), and the functions you are asked for
are:

- `uvg_sao_edge_ddistortion` — the edge-offset distortion
- `uvg_calc_sao_edge_dir` — the per-category edge histogram
- `uvg_sao_reconstruct_color` — the reconstruction pass, both the band and the
  edge path
- `uvg_sao_band_ddistortion` — the band-offset distortion

All of them are currently served by `src/strategies/generic/sao-generic.c`. The
optimized twins upstream ships for other instruction sets are not in this tree,
so `src/strategies/strategies-sao.c` registers only the generic implementation
and the encoder runs the C path for every one of them.

Optimize them however you judge best — C with intrinsics, hand-written
assembly, or a better portable C implementation. You are measured on the
function the encoder actually installs for each one, so any route that makes it
faster is acceptable. If you add files, register them in
`src/strategies/strategies-sao.c` and add them to the `if(UVG_HAVE_X86_64_AVX2)`
source list in `src/CMakeLists.txt`, which is the list that applies the
project's AVX2 flags (`-mavx2 -mbmi -mpopcnt -mlzcnt -mbmi2`, from
`cmake/cpu.cmake`). A file left in the generic list is compiled without them.

**What success means here.**

1. **The encoder must behave exactly as it does now.** This is an optimization
   task, not a behaviour-change task: for every input and every configuration
   the encoded bitstream must be byte-for-byte what this tree produces today —
   not merely for the cases you happen to test. These kernels pick a per-sample offset and add it, with a clip, so
the mapping is exact; a different clip order or a different edge category at a
block border changes the reconstruction and the bitstream with it. Do not edit the
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

- `src/strategies/strategies-sao.c` — the dispatcher and the registration
  function. Register new implementations here.
- `src/strategies/strategies-sao.h` — the exact signatures and the
  `sao_edge_ddistortion_func`, `calc_sao_edge_dir_func`,
  `sao_reconstruct_color_func` and `sao_band_ddistortion_func` typedefs.
- `src/strategies/generic/sao-generic.c` and
  `src/strategies/generic/sao_shared_generics.h` — the C implementation this
  replaces, and the reference for what is correct.
- `src/sao.c` — the offset array and how the encoder splits a picture into the
  blocks these kernels are called with; `sao_calc_eo_cat` and the edge-offset
  table live in `src/strategies/generic/sao_shared_generics.h`.
- `tests/` — this family has no dedicated differential test upstream, so the
  behaviour checks in `/app/BENCHMARK.md` are your gate.

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
