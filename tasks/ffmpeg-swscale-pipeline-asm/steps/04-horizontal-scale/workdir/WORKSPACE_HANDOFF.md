# Workspace handoff

## What you are starting from

`/app/ffmpeg` -- FFmpeg at a pinned release, already configured for this task.
Everything is upstream and intact except that some of the optimized kernels
libswscale uses are not present, so the library falls back to its portable C
implementation. It builds, it passes its own test suite, and it converts and
scales frames correctly; it is simply slower than it can be.

## What this milestone is

Horizontal polyphase scaling -- the horizontal polyphase scaler: a short fixed-point dot product per output pixel, with per-pixel filter offsets.

## What is in the workspace

- `/app/ffmpeg` -- the tree. `/app/build.sh` configures and builds it into
  `/app/ffmpeg/build`, which is not carried forward, so build it again if it is
  missing.
- `/app/tools/` -- an end-to-end `sws_scale` driver: `measure_sws.c` and
  `build_measure.sh`, which rebuilds the binary against whatever the tree
  currently is.
- `SCORED_FUNCTIONS.txt` -- the exact set of functions this milestone is scored
  on.
- `BENCHMARK.md`, in this step's workspace -- how to build, how behaviour is
  checked, how speed is measured, and which functions are scored.
- `instruction.md` -- what this milestone asks for.

## What to leave behind

A tree that builds with `/app/build.sh`, whose `checkasm` differential suite
still passes, and in which this milestone's stage installs optimized kernels of
its own.
