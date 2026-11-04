# Step 5 — Quantisation

## 1. Implement this milestone

`/app/uvg266` is the uvg266 VVC encoder at a pinned upstream commit with the
optimized implementation of one or more strategy families removed. The encoder
is complete and correct: it builds, passes its own test suite, and produces
valid bitstreams using the portable C implementations in
`src/strategies/generic/`. It is simply slow where the optimized code is
missing.

**This milestone: make quantisation and dequantisation faster, without changing a single output byte.**

The family is `src/strategies/generic/quant-generic.c`, and the functions you are
asked for are:

- `uvg_quant` — the scalar quantiser. The measured draw runs it through its
  ordinary path, with the optional sign-hiding pass off, which is what the
  encoder's default configuration uses. `transform_skip` is drawn both on and
  off; the other optional passes stay off.
- `uvg_dequant` — the matching dequantiser
- `uvg_quantize_residual` — the fused transform-and-quantise entry point the
  encoder's search calls in a loop
- `uvg_coeff_abs_sum`, `uvg_fast_coeff_cost` — the coefficient reductions the
  rate model is built on

All of them are currently served by `src/strategies/generic/quant-generic.c`. The
optimized twins upstream ships for other instruction sets are not in this tree,
so `src/strategies/strategies-quant.c` registers only the generic implementation
and the encoder runs the C path for every one of them.

Optimize them however you judge best — C with intrinsics, hand-written
assembly, or a better portable C implementation. You are measured on the
function the encoder actually installs for each one, so any route that makes it
faster is acceptable. If you add files, register them in
`src/strategies/strategies-quant.c` and add them to the `if(UVG_HAVE_X86_64_AVX2)`
source list in `src/CMakeLists.txt`, which is the list that applies the
project's AVX2 flags (`-mavx2 -mbmi -mpopcnt -mlzcnt -mbmi2`, from
`cmake/cpu.cmake`). A file left in the generic list is compiled without them.

**What success means here.**

1. **The encoder must behave exactly as it does now.** This is an optimization
   task, not a behaviour-change task: for every input and every configuration
   the encoded bitstream must be byte-for-byte what this tree produces today —
   not merely for the cases you happen to test. Quantisation is integer arithmetic with a table-driven scale, so
this is equality, not tolerance: one level off by one changes the coded
coefficients and the bitstream with it. Do not edit the
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

- `src/strategies/strategies-quant.c` — the dispatcher and the registration
  function. Register new implementations here.
- `src/strategies/strategies-quant.h` — the exact signatures and the
  `quant_func`, `dequant_func`, `quant_residual_func`, `coeff_abs_sum_func` and
  `fast_coeff_cost_func` typedefs.
- `src/strategies/generic/quant-generic.c` — the C implementation this replaces,
  and the reference for what is correct.
- `src/transform.c`, `src/rdo.c`, `src/dep_quant.c` — how the encoder calls
  these, and the other in-tree quantisers that share the same scale tables and
  the `encoder_state_t` argument.
- `tests/coeff_sum_tests.c` — the suite's own differential coverage for the
  coefficient reductions.

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
