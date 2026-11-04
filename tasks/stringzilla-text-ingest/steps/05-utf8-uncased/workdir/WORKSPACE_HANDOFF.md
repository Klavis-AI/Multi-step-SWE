# Workspace handoff

This milestone runs in the same `/app` worktree as the last one, but may begin a
brand-new conversation with no memory of it. **The source tree in
`/app/stringzilla` is your starting checkpoint** — whatever the previous
milestone left. Do not reset it or undo work already there: earlier work is
re-checked for correctness here.

## Your target this milestone

Search the corpus without pre-folding it — case-insensitive search that folds on the fly rather than pre-folding the haystack.

The family currently has no optimized backend: its backend files are gone and
the dispatch falls through to the portable implementation, which lives beside
them in `include/stringzilla/utf8_uncased.h` and `include/stringzilla/utf8_uncased/`. You may deliver the optimization by any route — a new header
of your own beside the portable one, a rewritten portable implementation,
whatever makes it faster. The measurement calls the entry point itself, through
the library's own dispatch, so any route that makes it faster counts.

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
