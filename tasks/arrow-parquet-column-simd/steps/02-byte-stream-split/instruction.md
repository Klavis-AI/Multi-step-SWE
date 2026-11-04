# Byte-stream-split encode and decode

## 1. Implement this milestone

`/app/arrow` is Apache Arrow at a pinned upstream commit with the optimized
implementation of one or more kernels removed. The library is complete and
correct: it builds, passes its own 1,783-assertion test suite, and reads and
writes Parquet and joins tables correctly using the portable scalar code paths.
It is simply slow in the places whose optimized code is missing.

**This milestone: make Arrow's byte-stream-split codec faster, without changing
a single byte it writes or reads back.**

The kernel is the Parquet BYTE_STREAM_SPLIT encoding:
`arrow::util::internal::ByteStreamSplitEncode(const uint8_t* raw_values, int
width, int64_t num_values, uint8_t* out)` and
`arrow::util::internal::ByteStreamSplitDecode(const uint8_t* data, int width,
int64_t num_values, int64_t stride, uint8_t* out)`, both in
`cpp/src/arrow/util/byte_stream_split_internal.h`. Encode is a strided byte
transpose — it splits a run of `num_values` values of `width` bytes into `width`
byte streams — and decode merges `width` streams back into interleaved values.
It is the codec behind the Parquet BYTE_STREAM_SPLIT encoding. This milestone
covers it in both directions at widths 2, 4 and 8 — 6
functions in all, each enumerated in `/app/BENCHMARK.md`.

Both entry points are inline switches over `width`. For 2, 4 and 8 they
tail-call the declared `ByteStreamSplit{En,De}codeSimdDispatch<width>`, which is
defined in `cpp/src/arrow/util/byte_stream_split_internal.cc` over a
`DynamicDispatch` table. Those tables register only
`ARROW_DISPATCH_TARGET_NONE(&ByteStreamSplit{En,De}codeScalar<width>)` today, so
every call lands on the portable scalar loop — `DoSplitStreams` and
`DoMergeStreams`, also in that header.

**Width 16 is deliberately out of scope.** The entry points route width 16
straight to `ByteStreamSplitEncodeScalar<16>` / `ByteStreamSplitDecodeScalar<16>`,
and no architecture upstream has a SIMD kernel for it. There is nothing there
for an optimized implementation to replace: a kernel written for width 16 would
read 1.00x by construction, whatever it did. Width 1 is a `memcpy` in the entry
point and is out of scope for the same reason.

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
   cases you happen to test. An approximation, a fast path that is right "almost
   always", or a reordering that changes rounding is a failure, however fast it
   is. Do not edit the scalar reference implementations; they define the correct
   output.
2. **This kernel must get faster** on the workload named below, for all 6
   functions. Only this kernel is in scope. Optimizing some other part of the
   library is out of scope for this milestone, and so is effort that does not
   show up in the measurement.

`/app/BENCHMARK.md` gives the exact commands for both — the behaviour checks and
the timing — so you can confirm where you stand before finishing. The same
checks are re-run on fresh inputs afterwards.

**The measurement inputs are drawn at measurement time and are not disclosed.**
They are random draws from the distribution `/app/BENCHMARK.md` describes, taken
at a seed that is not disclosed — a draw you cannot read and cannot
choose, and knowing it would buy you nothing anyway: the harness hands every
timed call fresh input bytes. Optimize the kernel for the general case.

## 2. Read these local references

**Normative references, all local:**

- `cpp/src/arrow/util/byte_stream_split_internal.h` — the two entry points, the
  scalar reference they currently reach (`DoSplitStreams`, `DoMergeStreams`,
  `ByteStreamSplitEncodeScalar<N>`, `ByteStreamSplitDecodeScalar<N>`), and the
  `ByteStreamSplit{En,De}codeSimdDispatch<N>` declarations the entry points
  tail-call.
- `cpp/src/arrow/util/byte_stream_split_internal.cc` — the dispatch layer. Each
  `ByteStreamSplit{En,De}codeSimdDispatch<N>` holds a `DynamicDispatch` over a
  target table; the tables are where a wider arm is registered, and a target
  whose ISA the running CPU does not support is never selected.
- The scalar implementations — `DoSplitStreams`, `DoMergeStreams`,
  `ByteStreamSplitEncodeScalar<N>`, `ByteStreamSplitDecodeScalar<N>` and the
  `*ScalarDynamic` fallbacks, all in `byte_stream_split_internal.h` — are the
  scalar reference. It is the correctness oracle and the tree you are measured
  against; leave it alone.
- `cpp/src/arrow/util/dispatch_internal.h` — `DynamicDispatch`,
  `DispatchLevel` and the `ARROW_DISPATCH_TARGET_*` entries a target table is
  built from. A target whose ISA the running CPU does not support is never
  selected.
- `cpp/src/arrow/util/byte_stream_split_test.cc` — upstream's own tests for this
  kernel.
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
- **This kernel is measured at AVX2 or narrower.** The reference implementation
  uses 256-bit registers here, so you may too. The target host provides SSE2,
  SSE4.2, AVX2 and BMI2; it has **no AVX-512**, and code that emits AVX-512
  cannot run on it at all. The source files you change are inspected for
  AVX-512 constructs and for build flags that would enable them globally.
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
