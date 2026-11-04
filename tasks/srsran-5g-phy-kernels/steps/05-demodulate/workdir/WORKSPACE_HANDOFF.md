# Workspace handoff

This milestone runs in the same `/app` worktree as the last one, but may begin a
brand-new conversation with no memory of it. **The source tree in
`/app/srsran` is your starting checkpoint** — whatever the previous milestone
left. Do not reset it or undo work already there: earlier work is re-checked for
correctness here.

## Your target this milestone

QAM soft demodulation — the demodulation mapper: QPSK, 16-QAM, 64-QAM and 256-QAM soft demapping of equalised symbols to `int8` log-likelihood ratios with per-symbol noise-variance scaling.

In this tree that kernel has no optimized implementation: the portable code is
what the library installs. Make the implementation the library installs faster
by any route you judge best — intrinsics in a new file registered in the
component's `CMakeLists.txt`, intrinsics in the existing translation units, or
work on the portable implementation itself. The measurement calls whichever
implementation the library installs for its default selector, so any route that
makes it faster counts.

Other parts of the library may already be optimized, or may still be running
their portable paths. Neither is your concern here: only this milestone's
functions are measured.

## What is in the workspace

- `/app/srsran` — the source tree.
- `/app/build` — a prebuilt build directory for that tree.
- `/app/BENCHMARK.md` — how to build, check behaviour, and time your work.
- `/app/tools/bench_phy.cpp` — the per-function timing harness.
- `/app/tools/build_harness.sh` — builds it against your tree.
- `/app/timer.sh` — advisory countdown.

## Step log

Append what you changed and the speed difference you measured.
