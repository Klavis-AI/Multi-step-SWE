# Workspace handoff

This milestone runs in the same `/app` worktree as the last one, but may begin a
brand-new conversation with no memory of it. **The source tree in
`/app/boringssl` is your starting checkpoint** — whatever the previous milestone
left. Do not reset it or undo work already there: earlier work is re-checked for
correctness here.

## Your target this milestone

AES-GCM. The primitive group currently has no optimized implementation.

You may optimize by any route — hand-written assembly in a new file registered
in `gen/sources.cmake`, C with the compiler's vector extensions, or the portable
C itself. The measurement calls whichever implementation the library ends up
installing for each entry point, so any route that makes them faster counts.

Other parts of the library may still be running their portable paths. That is
not your concern here: only the entry points named in `/app/BENCHMARK.md` are
measured.

## What is in the workspace

- `/app/boringssl` — the source tree, already configured in `/app/build`.
- `/app/BENCHMARK.md` — how to build, check behaviour, and time your work.
- `/app/tools/measure` — per-function timing harness for this milestone
  (`/app/tools/measure /app/build`, and `--list` to enumerate the set).
- `/app/timer.sh` — advisory countdown.

## Step log

Append what you changed and the speed difference you measured.
