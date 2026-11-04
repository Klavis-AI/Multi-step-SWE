# 05 — Transition counting across a block, and across an XOR of two blocks

## 1. Implement this milestone

`/app/bitmagic` is BitMagic at a pinned upstream commit, with the optimized
implementation of one or more of its bit-block kernel families removed from
`src/bmavx2.h`. The library is complete and correct: it builds, it passes its
own stress suite (`tests/stress/t.cpp`), and every entry point in it works — the
portable C++ bodies in `src/bmfunc.h` run instead of hand-written AVX2 kernels.
It is simply slow in the places whose kernels are missing.

**This milestone: make 2 entry points below faster, without changing a single output bit.**

| entry point | signature |
| --- | --- |
| `calc_change` | `unsigned bm::bit_block_calc_change(const bm::word_t* block)` |
| `change_bc` | `void bm::bit_block_change_bc(const bm::word_t* block, unsigned* gc, unsigned* bc)` |

Every one of them is a `bm::` free function. Each has a `VECT_*` dispatch macro
in its body that selects the installed kernel; with the family removed, that
macro is gone and the portable branch runs.

The family these entry points belong to has neighbours in the same header, and
implementing them too is allowed — that is what the library's own optimized
builds do — but only the entry points in the table above are measured, so effort
spent elsewhere has no effect either way.

**Make it faster by any route you judge best** — AVX2 intrinsics in a kernel
header, hand-written inline assembly, or the portable C++ itself. You are
measured on the implementation the library *installs* for each entry point, so
any route that makes the installed code faster is acceptable. Add new headers if
you like; `src/bmavx2.h` is the header the measured build includes, and the
backend macros live in `src/bmsimd.h`.

**The entry points' names and signatures are part of the contract.** The
measurement harness calls exactly the signatures in the table above, from
outside the library, so a renamed or re-signatured entry point fails to build
and does not meet this milestone however fast it is. Everything behind those
names is yours.

Earlier milestones must keep working. This workspace is cumulative: whatever was
optimized in earlier milestones is still here and is still part of this
milestone's baseline, and a regression breaks inherited behaviour as well.

**What success means here.**

1. **The library must behave exactly as it does now.** This is an optimization
   task, not a behaviour-change task: every entry point must produce
   byte-identical output to the tree you started from, for every input and every
   block representation — bitmap, GAP, all-zero, all-one — and the library's
   serialized output must be unchanged byte for byte. An approximation, a fast
   path that is right "almost always", or a reordering that changes a result is
   a failure, however fast it is.
2. **These entry points must get faster**, and only these are in scope.
   Optimizing some other part of the library is out of scope for this
   milestone.

`/app/BENCHMARK.md` gives the exact commands for both — the behaviour checks and
the timing — so you can confirm where you stand before finishing. The same
checks are re-run on fresh inputs afterwards.

**The measurement inputs are not disclosed.** They are drawn from a seed that
is not published, over a disclosed family (block density
and word structure are varied — dense random, sparse and run-structured draws).
You cannot choose your own input, and tuning to one structure is not faster
across the workload. Optimize for the general case.

## 2. Read these local references

- `/app/bitmagic/src/bmfunc.h` — the portable C++ body of every entry point in
  the table, and the `VECT_*` dispatch sites that select a kernel. This is the
  correctness reference: it defines the correct output.
- `/app/bitmagic/src/bmavx2.h` — the AVX2 backend header. It still holds this
  library's non-kernel AVX2 plumbing; the removed families are gone from it, and
  from `bmsse2.h`, `bmsse4.h`, `bmsse_util.h` and `bmavx512.h`.
- `/app/bitmagic/src/bmsimd.h` — how a backend macro selects a header.
- `/app/bitmagic/docs/build.md` — upstream's own notes on the SIMD backends.
- `/app/bitmagic/tests/stress/t.cpp` — the project's randomized differential
  suite. Building and running it is a good way to check you broke nothing.
- `/app/BENCHMARK.md` — the exact behaviour and timing commands.
- `/app/tools/` — the measurement harness, and how to build and run it.
- `/app/WORKSPACE_HANDOFF.md` — what earlier milestones changed, and how to build
  and measure locally.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace. Keep `/app/bitmagic` as the source
  tree and keep the upstream build system working
  (`cmake -S /app/bitmagic -B <dir> -DCMAKE_BUILD_TYPE=Release -DBMOPTFLAGS=BMAVX2OPT`).
- **This milestone is measured on AVX2 or narrower.** The target host provides
  SSE2, AVX2 and BMI2 and has no AVX-512, and the reference implementation for
  every entry point above uses 256-bit registers. So 256-bit AVX2 is your
  ceiling: `_mm512_*`/`__m512*`/`zmm` and `#pragma GCC target` /
  `__attribute__((target(...)))` are not allowed, because either would let one
  header widen the instruction set past what the reference uses and the speedup
  would then compare two different machines. Only the lines you add are checked,
  so the tree's pre-existing code is never held against you.
- You do not have network access. **Do not fetch, copy, vendor, or transcribe
  BitMagic's own optimized implementation of these entry points, or any other
  project's, from the internet or a package mirror.** The optimization must be
  yours.
- Do not weaken, filter, rename, or disable tests.
- Leave a reproducible handoff in `/app/WORKSPACE_HANDOFF.md`: what you changed,
  the speedup you measured locally, and how to rebuild.

