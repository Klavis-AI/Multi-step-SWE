# Workspace handoff

This milestone runs in the same `/app` worktree as the last one, but may begin a
brand-new conversation with no memory of it. **The source tree in `/app/arrow`
is your starting checkpoint** — whatever the previous milestone left. Do not
reset it or undo work already there: earlier work is re-checked for correctness
here.

## Your target this milestone

Acero's swiss-join row materialisation:
`arrow::acero::RowArray::DecodeSelected(ResizableArrayData* target, int
column_id, int num_rows_to_append, const uint32_t* row_ids, MemoryPool* pool)`,
in `cpp/src/arrow/acero/swiss_join.cc` and declared public on `RowArray` in
`cpp/src/arrow/acero/swiss_join_internal.h`. It gathers one column of a set of
selected rows back out of row format into the output arrays — fixed-length
columns through `DecodeFixedLength`, variable-length columns through
`DecodeOffsets` and `DecodeVarLength`, plus `DecodeNulls` for the validity
bitmap.

Each of those four sub-kernels has an AVX2 counterpart that the library selects
from `hardware_flags_` (and the output row offset, which must be a multiple of
8), and none of them is defined in this tree. The in-scope set is the two public
entry points that reach them: one fixed-length column and one variable-length
column. This is the smallest kernel in scope — a gather, not a rewrite — so a
modest honest win is what it is worth.

You may optimize by any route — a new translation unit built through the
project's own runtime-AVX2 helpers, or the portable C itself. The measurement
calls whatever implementation the library ends up installing for each function,
so any route that makes it faster counts.

Other parts of the library may already be optimized, or may still be running
their portable paths. Neither is your concern here: only this kernel is measured.

## What is in the workspace

- `/app/arrow` — the source tree.
- `/app/build` — a build of it as you received it, already warm. It is
  excluded from the checkpoint collected for checking, so the tree is
  rebuilt from your sources; what it buys you is a fast first build here.
- `/app/BENCHMARK.md` — how to build, how to check behaviour, how the timing
  works, and the shape of the measurement inputs.
- `/app/SCOPE.md` — the exact list of functions in scope for this milestone.
- `/app/tools/kbench.cc` — the measurement harness, as source, because it links
  your build. Build it with
  `/app/tools/build_probe.sh /app/arrow /app/build /app/tools/kbench.cc /tmp/kbench`.
- `/app/tools/e2e.cc` — a Parquet read plus hash-join driver, for checking that
  the real pipeline still works and has not got slower.
- `/app/tools/build_probe.sh` — the compile line both of the above use.
- `/app/timer.sh` — advisory countdown.

## Step log

Append what you changed and the speed difference you measured.
