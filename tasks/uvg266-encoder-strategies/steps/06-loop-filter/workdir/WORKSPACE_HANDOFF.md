# Workspace handoff

This milestone runs in the same `/app` worktree as the last one, but may begin a
brand-new conversation with no memory of it. **The source tree in `/app/uvg266`
is your starting checkpoint** — whatever the previous milestone left. Do not
reset it or undo work already there: earlier work is re-checked for correctness
here.

## Your target this milestone

Sample adaptive offset. In `src/strategies/strategies-sao.c` only the generic implementation
is registered, so `src/strategies/generic/sao-generic.c` serves every one of them.
The exact list is in the step instruction.

You may optimize by any route — intrinsics, assembly in a new file registered in
`src/CMakeLists.txt`, or the portable C itself. The measurement calls whichever
implementation the encoder ends up installing for each function, so any route
that makes it faster counts.

Other parts of the encoder may already be optimized, or may still be running
their C paths. Neither is your concern here: only this family is measured.

## What is in the workspace

- `/app/uvg266` — the source tree.
- `/app/BENCHMARK.md` — how to build, check behaviour, and time your work.
- `/app/tools/bench.c` — the practice timing and comparison harness. Compile it
  against your own build (the command is in `BENCHMARK.md`).
- `/app/tools/make_clip.py` — writes a deterministic raw clip for the end-to-end
  checks.
- `/app/timer.sh` — advisory countdown.

## Step log

Append what you changed and the speed difference you measured.
