# Workspace handoff

This milestone runs in the same `/app` worktree as the last one, but may begin a
brand-new conversation with no memory of it. **The source tree in
`/app/svt-jpeg-xs` is your starting checkpoint** — whatever the previous
milestone left. Do not reset it or undo work already there: earlier work is
re-checked for correctness here.

## Your target this milestone

GCLI group coding and the coefficient histogram. The slots in scope are the ones in
`Source/Lib/Encoder/Codec/encoder_dsp_rtcd.h`:

- `gc_precinct_stage_scalar` — per-group greatest-coded-line-index extraction over a precinct line
- `gc_histogram_16` — the 16-bin coefficient histogram the rate allocator consumes

All of them are installed with `SET_ONLY_C` in `Source/Lib/Encoder/Codec/encoder_dsp_rtcd.c`
today, so the portable C runs instead.

You may optimize by any route — AVX2/SSE intrinsics or plain C in a new file
under `Source/Lib/Encoder/ASM_AVX2/` (the directory globs `*.c`, so a
reconfigure picks it up), installed for the slot in the dispatch table. The
measurement calls whichever implementation the library ends up installing for
each slot, against the portable C reference the tree holds for it, so a faster
installed implementation is what counts.
**Two further slots of this stage are deliberately out of scope here.**
`gc_precinct_stage_scalar_loop` and `gc_precinct_sigflags_max` are part of the
same stage, but the reference implementation this task is calibrated against has
no AVX2 body for either — at the AVX2 ceiling both of its versions measure the
same as the portable C on this machine, so measuring them would pay for work no
measurement can see. They are not measured, and not doing anything to them costs
you nothing. If you can beat the C reference on them, nothing stops you, but
this milestone does not ask for it.
Other parts of the library may already be optimized, or may still be running
their C paths. Neither earns you anything here: only this stage's slots are
in scope. The pipeline is still checked, though — the end-to-end guard times a
full encode against the tree with none of these stages optimized — so leave the
rest of the library as you found it and do not slow any of it down.

## What is in the workspace

- `/app/svt-jpeg-xs` — the source tree.
- `/app/BENCHMARK.md` — how to build, check behaviour, and time your work.
- `/app/tools/bench/` — the per-function harness source; rebuild it against your
  tree with `bash /app/tools/build_bench.sh`.
- `/app/tools/codecbench-base`, `/app/tools/l0/` — the harness and the
  applications of the tree as it was before any milestone, for a fixed baseline.
- `/app/samples/1080p_422_8b.yuv` — a sample picture for your own runs.
- `/app/timer.sh` — advisory countdown.

## Step log

Append what you changed and the speed difference you measured.
