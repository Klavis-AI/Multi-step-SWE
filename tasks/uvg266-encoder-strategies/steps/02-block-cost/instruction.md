# Step 2 — block cost and distortion

## 1. Implement this milestone

`/app/uvg266` is the uvg266 VVC encoder at a pinned upstream commit with the
optimized implementation of one or more strategy families removed. The encoder
is complete and correct: it builds, passes its own test suite, and produces
valid bitstreams using the portable C implementations in
`src/strategies/generic/`. It is simply slow where the optimized code is
missing.

**This milestone: make the block-cost family faster, without changing a single
output byte.**

The distortion and cost kernels are what the encoder spends its search budget
on — every mode decision, every motion search step and every rate-distortion
trial goes through them. The family is `src/strategies/generic/picture-generic.c`, and
the functions you are asked for are:

- `uvg_reg_sad`, `uvg_sad_4x4`, `uvg_sad_8x8`, `uvg_sad_16x16`, `uvg_sad_32x32`,
  `uvg_sad_64x64` — sum of absolute differences, for one block and for a region
- `uvg_satd_4x4`, `uvg_satd_8x8`, `uvg_satd_16x16`, `uvg_satd_32x32`,
  `uvg_satd_64x64`, `uvg_satd_any_size`, `uvg_satd_any_size_quad` — Hadamard
  transforms and the four-mode variant
- `uvg_satd_4x4_dual` … `uvg_satd_64x64_dual` — the two-prediction cost
- `uvg_pixels_calc_ssd` — squared error
- `uvg_crc32c_4x4`, `uvg_crc32c_8x8` — the CRC used by the encoder's checksums
- `uvg_get_optimized_sad`, `uvg_ver_sad`, `uvg_hor_sad` — the width-specialised
  and directional SAD entry points
- `uvg_pixel_var`, `uvg_generate_residual`
- `uvg_bipred_average` — the two-prediction averaging kernel

All of them are currently served by `src/strategies/generic/picture-generic.c`.
The optimized twins upstream ships for other instruction sets are not in this
tree, so `src/strategies/strategies-picture.c` registers only the generic
implementation.

Optimize them however you judge best — C with intrinsics, hand-written
assembly, or a better portable C implementation. You are measured on the
function the encoder actually installs for each one, so any route that makes it
faster is acceptable. If you add files, register them in
`src/strategies/strategies-picture.c` and add them to the `if(UVG_HAVE_X86_64_AVX2)`
source list in `src/CMakeLists.txt`, which is the list that applies the
project's AVX2 flags (`-mavx2 -mbmi -mpopcnt -mlzcnt -mbmi2`, from
`cmake/cpu.cmake`). A file left in the generic list is compiled without them.

**What success means here.**

1. **The encoder must behave exactly as it does now.** This is an optimization
   task, not a behaviour-change task: for every input and every configuration
   the encoded bitstream must be byte-for-byte what this tree produces today.
   These kernels are pure integer reductions, so there is no rounding latitude
   at all — a kernel that is right "almost always" is a failure, however fast
   it is. Do not edit the generic implementations under `src/strategies/generic/`:
   they define the correct output.
2. **These functions must get faster**, measured individually by calling the
   implementation the encoder installs for each. Only these are in scope.

`/app/BENCHMARK.md` gives the exact commands for both. The same checks are
re-run against a fresh workload afterwards.

**The workload is not disclosed.** It is generated from a seed you are not given, and it draws the rectangular sizes and the two-prediction
layouts from published families rather than fixing them. Optimize for the
general case.

## 2. Read these local references

- `src/strategies/strategies-picture.c` — the dispatcher: the 33 global
  function pointers, the registration function, and the width-to-pointer
  helpers. Register new implementations here.
- `src/strategies/strategies-picture.h` — the exact signatures and the
  `cost_pixel_nxn_func`, `cost_pixel_nxn_multi_func`, `reg_sad_func`,
  `crc32c_*_func` typedefs, and the `pred_buffer` type the dual-cost kernels take.
- `src/strategies/generic/picture-generic.c` — the C implementation this
  replaces, and the reference for what is correct.
- `src/search_intra.c`, `src/search_inter.c`, `src/image.c` — how the encoder
  calls these, with what strides, and how the two-prediction buffers are laid
  out.
- `tests/sad_tests.c`, `tests/satd_tests.c`, `tests/intra_sad_tests.c` — the
  suite's own differential tests for this family.
- `/app/BENCHMARK.md` — the exact behaviour and timing commands.
- `/app/tools/bench.c` — the practice harness. Compile it against your build.
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
- Earlier milestones must keep working. This workspace is cumulative.
- The public network is available for official documentation and
  ordinary dependency lookup. **Do not fetch, copy, vendor, or transcribe
  uvg266's or any other project's optimized implementation of this family.**
  Do not inspect `/tests`, later-step directories, or solution material.
- Do not weaken, filter, rename, or disable tests.
- Leave a reproducible handoff in `/app/WORKSPACE_HANDOFF.md`.
