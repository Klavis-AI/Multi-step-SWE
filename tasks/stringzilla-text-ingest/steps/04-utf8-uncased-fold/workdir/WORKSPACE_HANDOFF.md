# Workspace handoff

This milestone runs in the same `/app` worktree as the last one, but may begin a
brand-new conversation with no memory of it. **The source tree in
`/app/stringzilla` is your starting checkpoint** — whatever the previous
milestone left. Do not reset it or undo work already there: earlier work is
re-checked for correctness here.

## Your target this milestone

Case-fold the corpus — Unicode case folding — rewriting UTF-8 into its case-folded form.

The family currently has no optimized backend: its backend files are gone and
the dispatch falls through to the portable implementation, which lives beside
them in `include/stringzilla/utf8_uncased_fold.h` and `include/stringzilla/utf8_uncased_fold/`. You may deliver the optimization by any route — a new header
of your own beside the portable one, a rewritten portable implementation,
whatever makes the entry points faster. The measurement calls the entry points
themselves, through the library's own dispatch, so any route that makes them
faster counts.

Other parts of the library may already be optimized, or may still be running
their portable paths. Neither is your concern here: only this milestone's entry
points are measured.

## What is in the workspace

- `/app/stringzilla` — the source tree.
- `/app/BENCHMARK.md` — how to build, check behaviour, and time your work, and
  which entry points this milestone is timed on.
- `/app/tools/measure` — timing tool for individual entry points;
  `/app/tools/build.sh` rebuilds it against your headers.
- `/app/timer.sh` — advisory countdown.

## Step log

Append what you changed and the speed difference you measured.
