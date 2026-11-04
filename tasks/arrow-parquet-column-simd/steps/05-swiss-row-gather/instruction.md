# Swiss-join row materialisation

## 1. Implement this milestone

`/app/arrow` is Apache Arrow at a pinned upstream commit with the optimized
implementation of one or more kernels removed. The library is complete and
correct: it builds, passes its own 1,783-assertion test suite, and reads and
writes Parquet and joins tables correctly using the portable scalar code paths.
It is simply slow in the places whose optimized code is missing.

**This milestone: make the swiss join's row materialisation faster, without
changing a single value it writes into the output arrays.**

Once a swiss join has decided which rows match, it still has to get those rows
back out of row format and into the output columns. That is
`arrow::acero::RowArray::DecodeSelected(ResizableArrayData* target, int
column_id, int num_rows_to_append, const uint32_t* row_ids, MemoryPool* pool)`
in `cpp/src/arrow/acero/swiss_join.cc`, declared public in
`cpp/src/arrow/acero/swiss_join_internal.h`. For one column and a list of row
ids it fills the output buffers: fixed-length columns through
`DecodeFixedLength`, variable-length columns through `DecodeOffsets` and
`DecodeVarLength`, and both through `DecodeNulls` for the validity bitmap.

Each of those four sub-kernels has an AVX2 counterpart — declared on `RowArray`
in `swiss_join_internal.h`, selected when `hardware_flags_ & CpuInfo::AVX2` and
the output row offset is a multiple of 8, with the portable sub-kernel taking
whatever remainder is left — and none of them is defined in this tree. This
milestone covers the two public entry points that reach them, one fixed-length
column gather and one variable-length one, each enumerated in
`/app/BENCHMARK.md`.

Be clear about the size of this one: it is the smallest kernel in scope. It is
two functions through one public entry point, doing a strided gather out of the
row layout — real work, but a gather, not a rewrite, and an honest win here is a
modest one. Nothing else in the materialisation path is in scope.

Optimize the kernel however you judge best — a new translation unit built with
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
   cases you happen to test. The output arrays, their validity bitmaps and their
   offsets must come out exactly as before, for every row id list and every
   output offset. Do not edit the scalar reference implementations; they define
   the correct output.
2. **These functions must get faster** on the workload named below, at both
   entry points. Only these functions are in scope. Optimizing some other part
   of the library is out of scope for this milestone, and so is effort that does
   not show up in the measurement.

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

- `cpp/src/arrow/acero/swiss_join.cc` — `RowArray::DecodeSelected` and the
  materialisation path it drives, including how the four sub-kernels are reached
  and what each of them writes.
- `cpp/src/arrow/acero/swiss_join_internal.h` — `RowArray`: the public
  `DecodeSelected` declaration, the `hardware_flags_` member the arm is chosen
  from, and the wider sub-kernel declarations the class already carries. The
  AVX2 sub-kernels themselves are not in this tree.
- `cpp/src/arrow/compute/light_array_internal.h` — `ResizableArrayData`, the
  output container `DecodeSelected` fills in: its fixed-length buffer, its
  varying-length buffer and its validity bitmap, and how it is resized.
- `cpp/src/arrow/compute/row/encode_internal.h` and `row_encoder_internal.h` —
  the row layout the gather reads from: how a row is laid out and how the
  encoder puts it there, which is what decides where each column's bytes are.
- `cpp/src/arrow/util/cpu_info.h` — `CpuInfo::AVX2` and the `hardware_flags`
  word the library composes from it; a CPU that does not support an ISA cannot
  be handed its flag, so a wider arm is only reached where it can run.
- `cpp/src/arrow/acero/hash_join_node_test.cc` — upstream's tests that exercise
  the join this kernel ends in, and the way the materialisation path gets
  covered: the sub-kernels below `DecodeSelected` are private to `RowArray`, so
  there is no narrower unit test to run. This target and the end-to-end driver
  below are the behaviour checks you have.
- `cpp/src/arrow/CMakeLists.txt` — the `append_runtime_avx2_src` /
  `append_runtime_avx512_src` helpers and the per-file `-march` options the
  build system already applies to runtime-dispatched translation units. Adding a
  source file through them is how a wider kernel gets compiled without raising
  the baseline ISA of the whole library.
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
- **These functions are measured at AVX2 or narrower.** The reference
  implementation uses 256-bit registers here, so you may too. The target host
  provides SSE2, SSE4.2, AVX2 and BMI2; it has **no AVX-512**, and code that
  emits AVX-512 cannot run on it at all. The source files you change are
  inspected for AVX-512 constructs and for build flags that would enable them
  globally.
- **The measurement is single-threaded and pinned to one core.** Threads,
  tasks and async work cannot help and appear in the measurement as noise.
- You do not have network access. **Do not fetch, copy, vendor, or transcribe
  Apache Arrow's own optimized implementation of this kernel, or any other
  project's, from the internet or a package mirror.** The optimization must be
  yours. Do not inspect `/tests`, later milestone directories, or solution
  material.
- Do not weaken, filter, rename, or disable tests.
- Leave a reproducible handoff in `/app/WORKSPACE_HANDOFF.md`: what you changed,
  the speedup you measured locally, and how to rebuild.
