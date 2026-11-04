# Step 1 — Geometric resampling

## 1. Implement this milestone

`/app/Simd` is the Simd image-processing library at a pinned upstream release
with the implementations of one or more primitive families removed. The library
is complete and correct: it builds, passes its own test suite, and computes
every function correctly using the portable scalar code in
`src/Simd/SimdBase*.cpp`. It is simply slow in the places whose optimized code
is missing.

**This milestone: make one family of image primitives faster, without changing
a single output byte.** The family is the resizer: the byte, float and 16-bit bilinear, area, bicubic and nearest resamplers.

The family currently has no optimized implementation. Its per-ISA files and the
dispatch branches in `src/Simd/SimdLib.cpp` that selected them are absent, so the
library falls through to `Base::`. Put an implementation back — intrinsics,
portable C, whatever you judge best — and wire it into the dispatch ladder. You
are measured on the entry point the library actually dispatches to, so any route
that makes it faster is acceptable.

Earlier milestones must keep working. This workspace is cumulative: families you
optimized in earlier steps are still here and are still part of this step's
baseline, so a regression also breaks inherited behaviour.

**What success means here.**

1. **The library must behave exactly as it does now.** This is an optimization
   task, not a behaviour-change task: every function must produce byte-identical
   output to the tree you started from, for every input, size, channel count and
   parameter combination — not merely for the cases you happen to test. An
   approximation, a fast path that is right "almost always", or a reordering
   that changes rounding is a failure, however fast it is. Do not edit the
   scalar reference implementations under `src/Simd/SimdBase*.cpp`; they define
   the correct output.
2. **This family must get faster** on the functions named in `/app/BENCHMARK.md`.
   Only those functions are in scope. Optimizing some other part of the library is
   out of scope for this milestone, and so is effort that does not show up in
   the measurement.

`/app/BENCHMARK.md` gives the exact commands for both — the behaviour checks and
the timing — so you can confirm where you stand before finishing. The same
checks are re-run against the same in-scope functions afterwards.

**The in-scope set is the list below — 1 function:**

- `ResizerInit`

Each is measured as the ratio of the scalar reference's time to the dispatched
entry point's time on the same input, so a function left alone contributes no
improvement.

`ResizerInit` is a factory: it builds a resizer object whose `Run` is what
the framework times, once per resampling method, channel type and scale
factor it enumerates. The work is in what the factory builds.

**The measurement geometry is fixed and is not disclosed.** The project's framework
takes the image size as a command-line argument; the measurement uses one that
is not in this document. The ratio is a property of the kernel rather than of the
size, so optimize for the general case. Nothing here is a secret you can learn:
the in-scope functions are listed, the instrument is in the tree, and the
correctness checks are the project's own.

## 2. Read these local references

**Normative references, all local:**

- `/app/Simd/src/Simd/SimdLib.cpp` — the public entry points and the dispatch
  ladders that choose an implementation. This is where a family's branches go
  back in.
- `/app/Simd/src/Simd/SimdBase*.cpp`, `SimdBase*.h` — the scalar reference for
  every function, and the definition of correct output.
- `/app/Simd/src/Simd/SimdSse41*.cpp`, `SimdAvx2*.cpp` — the per-ISA
  implementations of the families that are still in the library, which is the
  house pattern for the file you are about to write. `prj/cmake/x86.cmake`
  shows the per-ISA compile flags each file group gets.
- `/app/Simd/src/Test/` — the framework that checks and times every
  implementation. `Test.cpp` prints its command line options under `-?`.
- `/app/BENCHMARK.md` — the exact behaviour and timing commands, and the in-scope
  list.
- `/app/WORKSPACE_HANDOFF.md` — this milestone's brief, and how to build and
  measure locally.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace. Keep `/app/Simd` as the source tree
  and keep the upstream build system working with the command in
  `/app/BENCHMARK.md`. New implementation files are picked up automatically by
  the source glob; there is no file list to register.
- **The scalar reference is frozen.** `src/Simd/SimdBase*.cpp` and
  `src/Simd/SimdBase*.h` are restored from the pinned release before measurement.
  So are `src/Test/` (the measuring instrument) and `prj/` (the build system).
  Edit them and your changes are discarded; the measurement is taken against the
  pristine reference either way.
- **A call must compute its result.** Your implementation must do the work on
  every call. Caching, memoizing, or short-circuiting a repeated call on the
  grounds that the arguments look familiar — by pointer, by content, or by any
  other key — fails this milestone; so does any other trick whose speed comes
  from not doing the computation. The timing harness is in the tree and you can
  read exactly what it does; the intent of the measurement is a faster kernel,
  not a faster no-op.
- **This family is measured on SSE4.1 through AVX2.** The measurement host provides
  AVX2 (and SSE4.1, SSE2) and no AVX-512, so an AVX-512 body is never selected
  and never timed; `SIMD_AVX512=OFF` in the build command means it is not even
  compiled. The ceiling is a fairness rule — both sides of the comparison
  are measured on the same machine — and the milestone enforces it: an
  AVX-512 or AMX construct (an intrinsic, a vector or mask type, a register
  name, a target pragma, or a wider compiler flag) on a line you add or
  change in the files this family owns fails the milestone, whether or not
  the build compiles it. Do not add `-march=native` or wider flags: the
  per-ISA file groups already carry the project's own flags.
- You do not have network access. **Do not fetch, copy, vendor, or transcribe
  Simd's own optimized implementation of this family, or any other project's,
  from the internet or a package mirror.** The optimization must be yours. Do
  not inspect `/tests`, later-step directories, or solution material.
- Do not weaken, filter, rename, or disable tests.
- Leave a reproducible handoff in `/app/WORKSPACE_HANDOFF.md`: what you changed,
  the speedup you measured locally, and how to rebuild.
