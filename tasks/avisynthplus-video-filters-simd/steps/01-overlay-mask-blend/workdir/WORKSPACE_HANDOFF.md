# Workspace handoff

This milestone runs in the same `/app` worktree as the last one, but may begin a
brand-new conversation with no memory of it. **The source tree in
`/app/avisynthplus` is your starting checkpoint** — whatever the previous
milestone left. Do not reset it or undo work already there: earlier work is
re-checked for correctness here.

## Your target this milestone

Overlay's masked merge and blend operators. `Overlay` with a `mask` argument:
the mask is expanded, inverted and applied per lane before the two sources are
merged; and `Overlay`'s `multiply`, `darken`, `lighten` and plain `blend`
operators on the unmasked path. The other modes the filter parses — add,
subtract, difference, exclusion, hardlight and softlight
(`avs_core/filters/overlay/overlay.cpp` has all twelve) — are *not* part of
this milestone: this family's vectorized implementation covers the operators
above, so they are left alone.

In the reference C++ this family's call sites are guarded by a preprocessor
conditional that has been removed, so the portable C++ body runs instead. Every
other family is in the same state, or already has vectorized kernels; neither
is your concern here — only this family is measured.

You may optimize by any route: intrinsics in new files registered in
`avs_core/Files.cmake` the way the project's other vectorized sources are, or
the portable C++ itself. The measurement calls whichever implementation the
library ends up installing, so any route that makes it faster counts. AVX2 is
the ceiling; AVX-512 would not execute on the target host.

## What is in the workspace

- `/app/avisynthplus` — the source tree, at the pinned upstream commit minus the
  families this ladder has not restored yet.
- `/app/build` — a configured build tree for it.
- `/app/BENCHMARK.md` — how to build, check behaviour, and time your work.
- `/app/tools/measure` — the timing driver.
- `/app/timer.sh` — advisory countdown.

## Step log

Append what you changed and the speed difference you measured.
