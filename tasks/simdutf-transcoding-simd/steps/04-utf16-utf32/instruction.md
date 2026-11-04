# Milestone 4 — UTF-16 and UTF-32 in both directions

## 1. Implement this milestone

`/app/simdutf` is simdutf at a pinned upstream commit with the optimized
implementation of one or more operation families removed. The library is
complete and correct: it builds, passes its own 90-test suite, and transcodes
everything correctly using the portable scalar code. It is simply slow in the
places whose optimized code is missing.

**This milestone: uTF-16 and UTF-32 in both directions, without changing a single output byte.**

The surrogate join and split: UTF-16 to UTF-32 and UTF-32 to UTF-16,
both endiannesses. Every UTF-16 direction has to recognise a surrogate pair and
combine it into one code point, and every UTF-32 direction has to split one back
into two units — which means deciding, per lane, whether the next unit belongs
with this one.

It covers the validating forms, the `_with_errors` forms and the `valid_` forms
of all four conversions.

The scalar reference under `include/simdutf/scalar/` implements all of it and
is what the library falls back on today; it is also what your work is compared
against, so do not edit it. How you make this faster is up to you — the
project's SIMD template layer under `src/simdutf/` and the `simd::` helpers it
exposes are the idiom the rest of the library is written in, but any route that
makes the measured function faster counts, because the measurement calls
whichever implementation the library installs.

Earlier milestones must keep working. This workspace is cumulative: work you
did in earlier milestones is still here and is still part of this milestone's
baseline, so a regression breaks the earlier milestone as well as this one.

**What success means here.**

1. **The library must behave exactly as it does now.** This is an optimization
   task, not a behaviour-change task: transcoding must produce byte-identical
   output to the tree you started from, for every input and every configuration
   — not merely for the cases you happen to test. That includes the error
   position the library reports for invalid input, which is part of the
   contract and not a detail. An approximation, a fast path that is right
   "almost always", or a reordering that changes rounding is a failure, however
   fast it is. Do not edit the scalar reference implementations; they define
   the correct output.
2. **This milestone must get faster** on the workload described in
   `/app/BENCHMARK.md`. Only the functions named in that file are in scope.
   Optimizing some other part of the library does nothing for this milestone,
   and neither does effort that does not show up in the measurement.

`/app/BENCHMARK.md` gives the exact commands for both — the behaviour checks,
the timed set, and how to time it yourself — so you can confirm where you stand
before finishing. The same checks are re-run against inputs you never see.

**The workload is not disclosed.** The measurement harness generates its corpus
inside its own container from a seed you never see, and that corpus is a
different draw from the one in `/app/workload`. The *distribution* is public — the generator
is shipped, and `BENCHMARK.md` describes it — so you can tell exactly what
shape of input you must handle; which bytes will be measured, you cannot.
Optimize for the general case. A kernel that only wins on one configuration is
not enough.

## 2. Read these local references

**Normative references, all local:**

- `/app/simdutf/include/simdutf/implementation.h` — the public interface. Every
  function named in `BENCHMARK.md` is a virtual method of
  `simdutf::implementation`, and the library dispatches to whichever
  implementation it selected at runtime.
- `/app/simdutf/include/simdutf/scalar/` — the portable implementation you are
  replacing for this milestone, and the definition of correct output.
- `/app/simdutf/src/simdutf/` — the SIMD template layer each back-end
  instantiates; `src/haswell/` is the AVX2 one.
- `/app/workload/` — a sample corpus from the same distribution as the measured
  one. Not the measurement workload.
- `/app/BENCHMARK.md` — the build, the behaviour checks, the timed set, and the
  timing command.
- `/app/tools/build-measure.sh` — builds `/app/tools/measure`, the per-function
  timer. Re-run it after every rebuild: it links the library, so a stale binary
  measures the tree as it used to be.
- `/app/WORKSPACE_HANDOFF.md` — what earlier milestones changed, and how to
  build and measure locally.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace. Keep `/app/simdutf` as the source
  tree, keep the upstream build system working
  (`cmake -S /app/simdutf -B <dir> -DSIMDUTF_TESTS=ON`), and preserve the
  handoff documents.
- **This milestone runs on AVX2 or narrower.** The reference implementation
  is 256-bit AVX2, so you may use it. The target host provides SSE2 and AVX2
  and has no AVX-512: a kernel written against AVX-512 cannot run there at all,
  and any AVX-512 construct in the sources that build the AVX2 path fails the
  milestone.
- You do not have network access. **Do not fetch, copy, vendor, or transcribe
  simdutf's own optimized implementation of this milestone, or any other
  project's, from the internet, a package mirror, or any copy of one that
  happens to be sitting in this repository.** The optimization must be yours. Do not inspect `/tests`, later milestones' directories, or solution
  material.
- Do not weaken, filter, rename, or disable tests.
- Leave a reproducible handoff in `/app/WORKSPACE_HANDOFF.md`: what you changed,
  the speedup you measured locally, and how to rebuild.
