# Workspace handoff

This milestone runs in the same `/app` worktree as the last one, but may begin a
brand-new conversation with no memory of it. **The source tree in
`/app/svt-jpeg-xs` is your starting checkpoint** — whatever the previous
milestone left. Do not reset it or undo work already there: earlier work is
re-checked for correctness here.

## Your target this milestone

Non-linear transform (inverse) and output format conversion. The slots in scope are the ones in
`Source/Lib/Decoder/Codec/decoder_dsp_rtcd.h`:

- `linear_output_scaling_8bit` — inverse scale, round and clamp one whole component plane to 8-bit
- `linear_output_scaling_8bit_line` — the same for a single line
- `linear_output_scaling_16bit` — the same for a whole component plane at 10/12-bit
- `linear_output_scaling_16bit_line` — the same for a single line at 16-bit
- `linear_output_scaling_16bit_line_msb` — the same, writing MSB-aligned 16-bit samples

All of them are installed with `SET_ONLY_C` in `Source/Lib/Decoder/Codec/decoder_dsp_rtcd.c`
today, so the portable C runs instead.

You may optimize by any route — AVX2/SSE intrinsics or plain C in a new file
under `Source/Lib/Decoder/ASM_AVX2/` (the directory globs `*.c`, so a
reconfigure picks it up), installed for the slot in the dispatch table. The
measurement calls whichever implementation the library ends up installing for
each slot, against the portable C reference the tree holds for it, so a faster
installed implementation is what counts.
The plane-level slots and the line-level slots are separately measured: the
decoder uses the plane entry points on the ordinary path and the line entry
points where a slice boundary splits a component, so neither can stand in for
the other.
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
