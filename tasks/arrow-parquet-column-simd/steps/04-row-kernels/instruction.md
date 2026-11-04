# Row-format compare, hash and swiss-table probe

## 1. Implement this milestone

`/app/arrow` is Apache Arrow at a pinned upstream commit with the optimized
implementation of one or more kernels removed. The library is complete and
correct: it builds, passes its own 1,783-assertion test suite, and reads and
writes Parquet and joins tables correctly using the portable scalar code paths.
It is simply slow in the places whose optimized code is missing.

**This milestone: make the row-format kernels of Arrow's hash join faster,
without changing a single hash, match or filter decision.**

Arrow's swiss join keeps its build side in *row format* — one packed byte row
per key — and these six kernels are what it does with it. Each is enumerated in
`/app/BENCHMARK.md`:

- `row.swisstable.earlyfilter` — `arrow::compute::SwissTable::early_filter(int
  num_keys, const uint32_t* hashes, uint8_t* out_match_bitvector, uint8_t*
  out_local_slots)` in `cpp/src/arrow/compute/key_map_internal.cc`. For each
  32-bit hash it reads the starting 64-bit block of the table's status bytes and
  reports whether the hash's stamp can be present, writing one match bit and one
  local slot id per row. It batch-rejects rows that cannot match before the
  expensive comparison pass.
- `row.compare` — `arrow::compute::KeyCompare::CompareColumnsToRows(...)` in
  `cpp/src/arrow/compute/row/compare_internal.cc`. It compares a column batch
  against rows held in row format and produces a match bitmap, or the surviving
  indices when a selection is asked for. It walks the columns one at a time,
  fixed-width and variable-width and null-update cases separately, and folds
  their verdicts into one byte per row before turning it into the bitmap.
- `row.hashbatch` — `arrow::compute::Hashing32::HashBatch(const ExecBatch&
  key_batch, uint32_t* hashes, ..., int64_t hardware_flags,
  util::TempVectorStack* temp_stack, ...)`, and `row.hashmulticol` —
  `Hashing32::HashMultiColumn(const std::vector<KeyColumnArray>& cols,
  LightContext* ctx, uint32_t* hashes)`, both in
  `cpp/src/arrow/compute/key_hash_internal.cc`. These hash fixed-width keys
  (`HashFixed`) and variable-length keys (`HashVarLen`) into 32-bit hashes, plus
  the per-column combine and null handling that folds several columns into one
  row hash.

- `row.decode.binary` and `row.decode.varbinary` —
  `arrow::compute::detail::EncoderBinary::Decode(...)` and
  `EncoderVarBinary::Decode(...)` in
  `cpp/src/arrow/compute/row/encode_internal.cc`. These materialise a key
  column back out of row format: fixed-width keys are a strided copy per row,
  variable-length keys need the row's own inline offsets resolved first. This
  is the path the grouper runs once it has decided which rows survived.

**The arm is not chosen for you by the dispatcher.** None of these kernels goes
through `DynamicDispatch`. Each takes an explicit `hardware_flags` word and
branches on it — `hardware_flags & CpuInfo::AVX2` — and the library itself
supplies the word it reads from `CpuInfo`: `LightContext::hardware_flags` for
the compare and swiss-table probes, and a plain argument for `HashBatch`. You
are measured through that branch, so whatever implementation you install behind
it is what is timed, and the harness never names a scalar or a SIMD symbol.

**Row encode is out of scope.** Upstream vectorises only the row *decode* side
— the two `row.decode.*` functions above; there is no AVX2 row encode in this
tree or upstream, so there is nothing there for an optimized implementation to
replace and no function for it in scope.

Optimize the kernels however you judge best — new translation units built with
the per-file options the build system already uses for runtime-AVX2 sources, or
the portable C itself. You are measured on what the library actually installs,
so any route that makes it faster is acceptable.

Earlier milestones must keep working. This workspace is cumulative: work you did
in an earlier milestone is still here and is still part of this milestone's
baseline, so a regression breaks inherited behaviour as well as this
milestone's.

**What success means here.**

1. **The library must behave exactly as it does now.** This is an optimization
   task, not a behaviour-change task: reading and writing Parquet, decoding IPC,
   and every other observable output must be byte-identical to the tree you
   started from, for every input and every configuration — not merely for the
   cases you happen to test. A hash that differs in one bit, a match bitmap that
   differs in one lane, or a probe that admits a row the table cannot hold is a
   failure, however fast it is. Do not edit the scalar reference
   implementations; they define the correct output.
2. **These kernels must get faster** on the workload named below, at all six
   functions. Only these kernels are in scope. Optimizing some other part of the
   library is out of scope for this milestone, and so is effort that does not
   show up in the measurement.

`/app/BENCHMARK.md` gives the exact commands for both — the behaviour checks and
the timing — so you can confirm where you stand before finishing. The same
checks are re-run on fresh inputs afterwards.

**The measurement inputs are drawn at measurement time and are not disclosed.**
They are random draws from the distribution `/app/BENCHMARK.md` describes, taken
at a seed that is not disclosed — a draw you cannot read and cannot
choose, and knowing it would buy you nothing anyway: the harness hands every
timed call fresh input bytes. Optimize the kernels for the general case.

## 2. Read these local references

**Normative references, all local:**

- `cpp/src/arrow/compute/key_map_internal.cc` and `key_map_internal.h` —
  `SwissTable::early_filter`, the scalar implementation it currently calls
  (`early_filter_imp`), and the wider arms the class already carries
  declarations for.
- `cpp/src/arrow/compute/key_hash_internal.cc` and `key_hash_internal.h` —
  `Hashing32::HashBatch`, `HashMultiColumn`, `HashFixed` and `HashVarLen`. They
  carry the batch loop, the temp-stack usage and the null handling, so the
  combine semantics — which hash a row ends up with — are there, not in the
  inner loop.
- `cpp/src/arrow/compute/row/compare_internal.cc` and `compare_internal.h` —
  `KeyCompare::CompareColumnsToRows` and the per-type helpers behind it.
- `cpp/src/arrow/compute/row/encode_internal.cc` and `encode_internal.h` — the
  row decoding sub-kernels, kept here so you can see where encoding order and
  row layout come from. Row *encoding* is out of scope.
- `cpp/src/arrow/compute/light_array_internal.h` — `LightContext`, the struct
  whose `hardware_flags` member is how these kernels learn which arm to take.
- `cpp/src/arrow/util/cpu_info.h` — `CpuInfo::AVX2` and the `hardware_flags`
  word the library composes from it. A CPU that does not support an ISA cannot
  be handed its flag, so a wider arm is only reached where it can run.
- `cpp/src/arrow/compute/key_hash_test.cc`,
  `cpp/src/arrow/compute/row/compare_test.cc` and
  `cpp/src/arrow/compute/row/row_test.cc` — upstream's own tests for these
  kernels.
- `cpp/src/arrow/CMakeLists.txt` — the `append_runtime_avx2_src` /
  `append_runtime_avx2_bmi2_src` / `append_runtime_avx512_src` helpers and the
  per-file `-march` options the build system already applies to
  runtime-dispatched translation units. Adding a source file through them is how
  a wider kernel gets compiled without raising the baseline ISA of the whole
  library.
- `/app/BENCHMARK.md` — the exact behaviour and timing commands, and the list of
  in-scope functions.
- `/app/WORKSPACE_HANDOFF.md` — what is in the workspace and how to build and
  measure locally.
- `/app/tools/kbench.cc` — the measurement harness, as source, because it links
  the candidate's own build. Build it with
  `/app/tools/build_probe.sh /app/arrow /app/build /app/tools/kbench.cc /tmp/kbench`
  and run `/tmp/kbench samples <seed> all`.

The float aggregate kernels (`compute/kernels/aggregate_basic_avx2.cc` and its
AVX-512 sibling) are **not** your concern. They reassociate floating-point sums
and are deliberately excluded from this ladder: behaviour here must match
exactly, so no milestone can ask for them. Leave them alone.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace. Keep `/app/arrow` as the source tree,
  keep the upstream build system working, and preserve the handoff documents.
  The build command is the one in `/app/BENCHMARK.md`; the measurement harness
  uses exactly that configuration.
- **These kernels are measured at AVX2 or narrower.** The reference
  implementation uses 256-bit registers here, so you may too. The target host
  provides SSE2, SSE4.2, AVX2 and BMI2; it has **no AVX-512**, and code that
  emits AVX-512 cannot run on it at all. The source files you change are
  inspected for AVX-512 constructs and for build flags that would enable them
  globally.
- **The measurement is single-threaded and pinned to one core.** Threads,
  tasks and async work cannot help and appear in the measurement as noise.
- You do not have network access. **Do not fetch, copy, vendor, or transcribe
  Apache Arrow's own optimized implementation of these kernels, or any other
  project's, from the internet or a package mirror.** The optimization must be
  yours. Do not inspect `/tests`, later milestone directories, or solution
  material.
- Do not weaken, filter, rename, or disable tests.
- Leave a reproducible handoff in `/app/WORKSPACE_HANDOFF.md`: what you changed,
  the speedup you measured locally, and how to rebuild.
