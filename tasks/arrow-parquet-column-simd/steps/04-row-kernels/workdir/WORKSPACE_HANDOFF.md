# Workspace handoff

This milestone runs in the same `/app` worktree as the last one, but may begin a
brand-new conversation with no memory of it. **The source tree in `/app/arrow`
is your starting checkpoint** — whatever the previous milestone left. Do not
reset it or undo work already there: earlier work is re-checked for correctness
here.

## Your target this milestone

Six of the row-format kernels behind Arrow's swiss join:

- `arrow::compute::SwissTable::early_filter` —
  `cpp/src/arrow/compute/key_map_internal.cc`.
- `arrow::compute::KeyCompare::CompareColumnsToRows` —
  `cpp/src/arrow/compute/row/compare_internal.cc`.
- `arrow::compute::Hashing32::HashBatch` and
  `arrow::compute::Hashing32::HashMultiColumn` —
  `cpp/src/arrow/compute/key_hash_internal.cc`.
- `arrow::compute::detail::EncoderBinary::Decode` and
  `arrow::compute::detail::EncoderVarBinary::Decode` — the row-decode side, in
  `cpp/src/arrow/compute/row/encode_internal.cc`.

Unlike the other kernels in this workspace, none of these is reached through
`DynamicDispatch`: each takes an explicit `hardware_flags` word, supplied by the
library from `CpuInfo` (`LightContext::hardware_flags` for the compare, the
swiss-table probe and the two row decoders, a plain argument for `HashBatch`),
and branches on `hardware_flags & CpuInfo::AVX2`. Whatever you install behind
that branch is what gets measured. Row encode has no AVX2 arm upstream and is out of
scope.

You may optimize by any route — new translation units built through the
project's own runtime-AVX2 helpers, or the portable C itself. The measurement
calls whatever implementation the library ends up installing for each function,
so any route that makes it faster counts.

Other parts of the library may already be optimized, or may still be running
their portable paths. Neither is your concern here: only these kernels are
measured.

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
