# Workspace handoff — milestone 04

This milestone may begin a brand-new conversation with no memory of earlier
work. **The source tree in `/app/bitmagic` is your starting checkpoint** —
whatever is already in it. Do not reset it or undo work already there: anything
already implemented is re-checked for correctness here, and only the entry
points listed in `/app/BENCHMARK.md` are measured.

## Your target this milestone

XOR block differencing for XOR compression. The entry points belong in `/app/bitmagic/src/bmxor.h`;
with their kernel families removed, the `VECT_*` dispatch macro is gone from
each and the portable C++ body in `src/bmfunc.h` runs instead.

You may optimize by any route — AVX2 intrinsics in a kernel header, hand-written
inline assembly, or the portable C++ itself. The measurement calls whichever
implementation the library ends up installing for each entry point, so any
route that makes it faster counts. The entry points' names and signatures are
part of the contract; everything behind them is yours.

Other parts of the library may already be optimized, or may still be running
their C paths. Neither is your concern here: only the 2 in-scope entry points
listed in `/app/BENCHMARK.md` are measured.

## What is in the workspace

- `/app/bitmagic` — the source tree.
- `/app/BENCHMARK.md` — the in-scope set, how to build, how to check behaviour,
  and how to time your work.
- `/app/tools/bench_kernels.cpp`, `/app/tools/e2e_bench.cpp` — the measurement
  harness and the end-to-end composite driver.
- `/app/tools/build.sh` — builds both against `/app/bitmagic`.
- `/app/timer.sh` — advisory countdown.

## Step log

Append what you changed and the speed difference you measured.

