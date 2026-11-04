# Workspace handoff

## What this is

`/app/parasail` is the parasail sequence-alignment library at a pinned
upstream commit, with the vectorized implementations of one or more alignment
families removed and their dispatchers rewired to fall through to the
project's own non-vectorized reference.  The library builds, passes its own
suite and aligns every pair correctly; it is slow wherever the optimized code
is missing.

`/app/build` is not shipped pre-built: run the `meson setup` command in
`/app/BENCHMARK.md` to create it.  `/app/tools/pbench` is the measurement
tool.  `/app/BENCHMARK.md` has the build, behaviour-check and timing commands.

## Layout

- `/app/parasail` -- the source tree you work in.
- `/app/build` -- build directory (`meson setup /app/build /app/parasail
  --buildtype=release && ninja -C /app/build`).  That command creates it; it is
  not carried between steps, so run it again whenever the directory is missing.
- `/app/tools/pbench` -- the compiled measurement tool.  It loads whichever
  `libparasail.so` you point it at, so it does not need rebuilding when you
  change the library.
- `/app/BENCHMARK.md`, `/app/SCOPE.txt`, `/app/WORKSPACE_HANDOFF.md` -- this
  brief.

## Rules that are graded

- Behaviour must not change: every entry point returns byte-identical results
  to the tree you started from, for every input.
- Every entry point you implement keeps the solution width it is named for.
- The non-vectorized reference implementations under `src/`
  (`sw.c`, `nw.c`, `sg.c`, `*_stats.c`, `*_trace.c`, `*_scan.c` and their
  generated `_table` / `_rowcol` / `_trace` variants) are the definition of
  correct output.  Do not change what they return; the reference build
  this is measured against uses the upstream sources, not yours.
- The instruction-set ceiling is AVX2 (256-bit).  There is no AVX-512 on the
  grading host.
- Do not fetch, copy, vendor or transcribe parasail's own optimized
  implementation of this family from the internet or a package mirror.

## Handing off

Update this file when you finish: what you changed, the speedup you measured
locally with `pbench`, and how to rebuild.  The next step starts from this
workspace as it is.
