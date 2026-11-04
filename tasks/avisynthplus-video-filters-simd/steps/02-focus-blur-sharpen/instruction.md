# Step 2 — Spatial and temporal smoothing

## 1. Implement this milestone

`/app/avisynthplus` is AviSynth+ at a pinned upstream commit with the
vectorized implementation of one or more filter families removed. The library
is complete and correct: it builds with upstream's own build command and
produces every filter's output from the portable C++ paths. It is simply slow
in the places whose optimized code is missing.

**This milestone: make spatial and temporal smoothing faster, without changing a single output byte.**

It covers `Blur`, `Sharpen` and `TemporalSoften`.  `SpatialSoften` is *not* part of this milestone: its vectorized implementation is slower than the portable C++ on every input measured, so it is left alone.

The family currently has no vectorized implementation. Every call site in the
reference C++ that would select a vectorized kernel is guarded by a
preprocessor conditional that has been removed, so the C++ body runs instead.
Optimize it however you judge best — intrinsics in new files registered the way
the project registers its other vectorized sources, or the portable C++ itself.
You are measured on the implementation the library actually installs for each
operation, so any route that makes it faster is acceptable.

Earlier milestones must keep working. This workspace is cumulative: families
you optimized in earlier steps are still here and are still part of this step's
baseline, so a regression breaks inherited behaviour as well as this step's.

**What success means here.**

1. **The library must behave exactly as it does now.** This is an optimization
   task, not a behaviour-change task: every filter must produce byte-identical
   output to the tree you started from, for every input and every
   configuration — not merely for the cases you happen to test. An
   approximation, a fast path that is right "almost always", or a reordering
   that changes rounding is a failure, however fast it is. Do not edit the
   portable C++ bodies to change what they compute; they define the correct
   output.
2. **This family must get faster** on the operations named above. Only this
   family is in scope. Optimizing some other part of the library is out of
   scope for this milestone, and so is effort that does not show up in the
   measurement.

`/app/BENCHMARK.md` gives the exact commands for both — the behaviour checks
and the timing — so you can confirm where you stand before finishing. The same
checks are re-run on fresh configurations afterwards.

**The measured configuration is fixed and is not disclosed.** The operations
above are what is timed; which frame sizes, pixel formats and filter
parameters are used is not published, and is the same in every run. Optimize
for the general case rather than for one setting: the timed units are not all
the same configuration, and a kernel that only wins on one of them is not faster
across the workload.

## 2. Read these local references

**Normative references, all local:**

- `avs_core/filters/focus.cpp`
- `avs_core/filters/focus.h`

- `/app/avisynthplus/avs_core/Files.cmake` — how the project's sources are
  registered in the build.
- `/app/avisynthplus/CMakeLists.txt`, `avs_core/CMakeLists.txt` — how the
  project turns on intrinsics per source file, and the flags it uses.
- `/app/BENCHMARK.md` — the exact behaviour and timing commands.
- `/app/tools/measure` — the timing driver. Run it as
  `/app/tools/measure /app/build/avs_core <chain.avs> <source.avs> <frames> <reps>`.
- `/app/WORKSPACE_HANDOFF.md` — what earlier milestones changed, and how to
  build and measure locally.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace. Keep `/app/avisynthplus` as the
  source tree, keep upstream's build system working
  (`cmake -S /app/avisynthplus -B /app/build -DCMAKE_BUILD_TYPE=Release -G Ninja`), and
  preserve the handoff documents.
- **This family is measured on AVX2 or narrower.** The reference implementation
  of every family in this ladder is 256-bit or narrower on the target machine,
  because the target host provides SSE2, SSE4.1 and AVX2 and no AVX-512. Do not
  add AVX-512: it would not execute, and it fails the milestone. The project
  already compiles a source file with the flags its
  name implies (`*_sse41.cpp` at `-msse4.1`, `*_avx2.cpp` at `-mavx2 -mfma`);
  use that convention. Do not add `-march=native` or a global `-mavx2`.
  **Both halves of this are checked, not assumed.** The files this milestone's
  implementation lives in -- the family's sources under `avs_core/filters/`,
  and any file you add beside them -- are scanned for AVX-512 constructs and
  for AVX-512 named as a compile target; and the flags your tree is actually
  compiled with are read from the build the measurement harness runs, where an
  ISA-raising flag on a translation unit whose *name* does not say so (the
  convention above) fails the milestone. See `/app/BENCHMARK.md`.
- Only the implementation may change. Leave the filter registration tables, the
  script-visible filter names and their argument lists alone: the measurement
  harness drives the library through its public C API, and a filter it cannot
  construct is a filter it cannot measure.
- You do not have network access. **Do not fetch, copy, vendor, or transcribe
  AviSynth+'s own vectorized implementation of this family, or any other
  project's, from the internet or a package mirror.** The optimization must be
  yours. Do not inspect `/tests`, later-step directories, or solution material.
- Do not weaken, filter, rename, or disable any check.
- Leave a reproducible handoff in `/app/WORKSPACE_HANDOFF.md`: what you
  changed, the speedup you measured locally, and how to rebuild.
