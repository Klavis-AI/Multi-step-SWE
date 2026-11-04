# Workspace handoff

This milestone may begin a brand-new conversation with no memory of the last one.

**The source tree in
`/app/simdutf` is your starting checkpoint** — whatever the previous milestone
left. Do not reset it or undo work already there: earlier work is re-checked for
correctness here.

## Your target this milestone

UTF-32 to UTF-8 encoding. Exactly which functions are timed, and on which corpus, is listed in
`/app/BENCHMARK.md`. The scalar reference under `include/simdutf/scalar/` is what
runs today and is the definition of correct output.

You may optimize by any route — the project's own SIMD idiom, plain C, or
anything else — as long as the library installs the result. The measurement
calls whichever implementation the library ends up installing, so any route that
makes the timed function faster counts.

Other parts of the library may already be optimized, or may still be running
their scalar paths. Neither is your concern here: only the functions
`BENCHMARK.md` names are measured.

## What is in the workspace

- `/app/simdutf` — the source tree.
- `/app/workload/` — a sample corpus; not the graded one.
- `/app/BENCHMARK.md` — how to build, check behaviour, enumerate the timed set
  and time your work.
- `/app/tools/bench_stage.cpp`, `/app/tools/build-measure.sh` — the per-function
  timer and its build script.
- `/app/timer.sh` — advisory countdown.

## Step log

Append what you changed and the speed difference you measured.
