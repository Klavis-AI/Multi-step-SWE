# Workspace handoff

This milestone runs in the same `/app` worktree as the last one, but may begin a
brand-new conversation with no memory of it. **The source tree in
`/app/svt-jpeg-xs` is your starting checkpoint** — whatever the previous
milestone left. Do not reset it or undo work already there: earlier work is
re-checked for correctness here.

## Your target this milestone

Forward 5/3 reversible wavelet transform. The slots in scope are the ones in
`Source/Lib/Encoder/Codec/encoder_dsp_rtcd.h`:

- `dwt_horizontal_line` — the horizontal lifting pass over one line, splitting it into low- and high-frequency halves
- `transform_vertical_loop_hf_line_0` — the first high-frequency line of the vertical pass
- `transform_vertical_loop_lf_line_0` — the first low-frequency line of the vertical pass
- `transform_vertical_loop_lf_hf_line_0` — the first line that produces both an LF and an HF output
- `transform_vertical_loop_lf_hf_line_x_prev` — an interior line, carrying LF and HF state from the previous two
- `transform_vertical_loop_lf_hf_hf_line_x` — an interior line that also folds in the previous high-frequency line
- `transform_vertical_loop_lf_hf_hf_line_last_even` — the closing line of the vertical pass
- `transform_V1_Hx_precinct_recalc_HF_prev` — the precinct-boundary recomputation of the carried HF line

All of them are installed with `SET_ONLY_C` in `Source/Lib/Encoder/Codec/encoder_dsp_rtcd.c`
today, so the portable C runs instead.

You may optimize by any route — AVX2/SSE intrinsics or plain C in a new file
under `Source/Lib/Encoder/ASM_AVX2/` (the directory globs `*.c`, so a
reconfigure picks it up), installed for the slot in the dispatch table. The
measurement calls whichever implementation the library ends up installing for
each slot, against the portable C reference the tree holds for it, so a faster
installed implementation is what counts.
The vertical kernels carry state between lines: read the reference for which
lines each slot consumes before you choose an interface.
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
