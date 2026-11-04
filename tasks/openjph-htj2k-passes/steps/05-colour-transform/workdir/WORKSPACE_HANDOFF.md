# Workspace handoff

This milestone runs in the same `/app` worktree as the last one, but may begin a
brand-new conversation with no memory of it. **The source tree in `/app/ojph` is
your starting checkpoint** — whatever the previous milestone left. Do not reset
it or undo work already there: earlier work is re-checked for correctness here.

## Your target this milestone

The colour transform, RCT and ICT. The library leaves the portable implementation
installed for this stage, so the code in `src/core/transform/ojph_colour.cpp` runs instead of an
optimized one.

You may optimize by any route — a new per-instruction-set translation unit
registered in `src/core/CMakeLists.txt`, intrinsics, assembly, or the portable
C++ itself. The measurement calls whichever implementation the library ends up
installing for each function, so any route that makes them faster counts.

Other parts of the library may already be optimized, or may still be running
their portable paths. Neither is your concern here: only this stage is measured.

## What is in the workspace

- `/app/ojph` — the source tree.
- `/app/workload/work.ppm` — a sample image for the behaviour checks. It is not
  the graded workload.
- `/app/BENCHMARK.md` — how to build, check behaviour, and time your work.
- `/app/tools/measure` — end-to-end timing harness (`/app/tools/measure /app/build`).
- `/app/timer.sh` — advisory countdown.

## Milestone log

Append what you changed and the speed difference you measured.
