# The split-block bloom filter probe

## 1. Implement this milestone

`/app/arrow` is Apache Arrow at a pinned upstream commit with the optimized
implementation of one or more kernels removed. The library is complete and
correct: it builds, passes its own 1,783-assertion test suite, and reads and
writes Parquet and joins tables correctly using the portable scalar code paths.
It is simply slow in the places whose optimized code is missing.

**This milestone: make Parquet's split-block bloom filter probe faster, without
changing a single answer it gives.**

The kernel is `parquet::BlockSplitBloomFilter::FindHash(uint64_t hash)` in
`cpp/src/parquet/bloom_filter.cc` — the probe a Parquet reader runs to decide
whether the data it is about to read can possibly contain a value. It reduces
the hash to a block index and a 32-bit key, then tests eight
bits of one 32-byte block: for each of the block's 8 words it computes
`1 << ((key * SALT[i]) >> 27)` and asks whether every one of those bits is set.
`SALT` is the format's fixed vector of eight odd constants, declared on
`BlockSplitBloomFilter` in `cpp/src/parquet/bloom_filter.h`, and
`kBitsSetPerBlock` is 8. One probe is one call and one `bool`.

`FindHash` dispatches the eight-word test through a
`DynamicDispatch<internal::FindHashBlockDynamicFunction>` table in
`bloom_filter.cc`, whose targets array registers only
`ARROW_DISPATCH_TARGET_NONE(&FindHashBlockImpl)` — the scalar loop in
`cpp/src/parquet/bloom_filter_block_impl_internal.h`. This milestone covers that
one function, enumerated in `/app/BENCHMARK.md`.

**Only the probe is in scope.** `InsertHashes` and the single-hash
`InsertHash`/`InsertHashImpl` path is a plain per-hash loop over the same eight
words, and has no dispatched SIMD arm anywhere upstream: there is nothing there
for an optimized implementation to replace, so the insert path is out of scope
and work on it has no effect here.

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
   cases you happen to test. A filter that answers "maybe" where the tree you
   inherited answered "no" is a wrong answer, however fast it is, so no
   approximation and no probabilistic shortcut. Do not edit the scalar reference
   implementation; it defines the correct output.
2. **This kernel must get faster** on the workload named below. Only this kernel
   is in scope. Optimizing some other part of the library is out of scope for
   this milestone, and so is effort that does not show up in the measurement.

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

- `cpp/src/parquet/bloom_filter.cc` — `FindHash`: the block index, the dispatch
  layer, and the target table a wider arm is registered in. A target whose ISA
  the running CPU does not support is never selected.
- `cpp/src/parquet/bloom_filter.h` — `BlockSplitBloomFilter`: `kBitsSetPerBlock`,
  the fixed `SALT` vector, `Init`, `OptimalNumOfBytes`, the `FindHash`
  declaration and the `InsertHashes` path that is out of scope.
- `cpp/src/parquet/bloom_filter_block_impl_internal.h` — `FindHashBlockImpl`, the
  scalar reference. It is the correctness oracle and the tree you are measured
  against; leave it alone.
- `cpp/src/parquet/bloom_filter_test.cc` — upstream's own tests for this kernel.
- `cpp/src/parquet/bloom_filter_benchmark.cc` — upstream's benchmark for it.
- `cpp/src/arrow/util/dispatch_internal.h` — `DynamicDispatch`, `DispatchLevel`
  and the `ARROW_DISPATCH_TARGET_*` entries a target table is built from.
- `cpp/src/parquet/CMakeLists.txt` — the `if(ARROW_HAVE_RUNTIME_AVX2)` block and
  its `AVX2_FLAGS` variable, the per-file options this build already applies to
  runtime-dispatched Parquet translation units. Adding a source file through
  them is how a wider kernel gets compiled without raising the baseline ISA of
  the whole library.
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
