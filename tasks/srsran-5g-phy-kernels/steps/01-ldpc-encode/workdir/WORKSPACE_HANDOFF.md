# Workspace handoff

This milestone runs in the same `/app` worktree as the last one, but may begin a
brand-new conversation with no memory of it. **The source tree in
`/app/srsran` is your starting checkpoint** — whatever the previous milestone
left. Do not reset it or undo work already there: earlier work is re-checked for
correctness here.

## Your target this milestone

LDPC encoding — the low-density parity-check encoder for base graphs 1 and 2 at lifting sizes 256, 320 and 384: the systematic-bit copy, the parity accumulation over the lifted circulants, and the cyclic rotation by the lifting size `Z`.

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
