# How to build, check and measure your work

## Build

Build with exactly this configuration:

```bash
cmake -S /app/arrow/cpp -B /app/build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
      -DARROW_PARQUET=ON -DARROW_ACERO=ON -DARROW_COMPUTE=ON \
      -DARROW_FILESYSTEM=ON -DARROW_IPC=ON -DARROW_DATASET=OFF \
      -DARROW_JSON=OFF -DARROW_CSV=OFF -DARROW_BUILD_TESTS=ON \
      -DARROW_BUILD_BENCHMARKS=OFF -DARROW_TEST_MEMCHECK=OFF \
      -DARROW_BUILD_STATIC=ON -DARROW_BUILD_SHARED=OFF \
      -DARROW_DEPENDENCY_SOURCE=AUTO -DThrift_SOURCE=SYSTEM \
      -DARROW_WITH_SNAPPY=OFF -DARROW_WITH_ZSTD=OFF -DARROW_WITH_LZ4=OFF \
      -DARROW_WITH_ZLIB=OFF -DARROW_WITH_BROTLI=OFF -DARROW_WITH_BZ2=OFF \
      -DARROW_WITH_UTF8PROC=OFF -DARROW_WITH_RE2=OFF
cmake --build /app/build -j"$(nproc)"
```

`/app/build` is already warm with the tree as you received it, so the first
build after your change is incremental.

## Behaviour must not change

The library has to produce exactly the bytes it produced before. All of these
are re-run, with the measurement inputs, when your work is checked.

1. **The function in this milestone** must answer identically to the version you
   inherited, for every key and every filter state. A probe that says "maybe"
   where the tree you inherited said "no" is a wrong answer, however fast it is.
   This is what the harness checks first, before it times anything:

   ```bash
   /app/tools/build_probe.sh /app/arrow /app/build /app/tools/kbench.cc /tmp/kbench
   taskset -c 0 /tmp/kbench dump 1 all > /tmp/now.txt
   ```

   Run it against the tree as you received it first, save that output, and
   compare after every change. `dump` prints one line per function:
   `<function> <checksum>`. The checksum is over the whole probe output, not a
   sample, so a single wrong answer anywhere shows up.

2. **The project's own suite** must gain no new failure. Build and run these
   targets; all but two of them pass in this image as delivered:

   ```bash
   for t in arrow-utility-test arrow-bit-utility-test parquet-encoding-test \
            parquet-reader-test parquet-internals-test arrow-compute-row-test \
            arrow-acero-hash-join-node-test; do
     /app/build/release/$t 2>&1 | tail -3
   done
   ```

   `parquet-reader-test` and `parquet-internals-test` are the two that do not,
   and it is the image's state rather than anything you did: both read the
   upstream `parquet-testing` data tree, which is **not** fetched into this
   image, so without `PARQUET_TEST_DATA` every data-driven case of theirs aborts
   in `SetUp()` with "Please point the PARQUET_TEST_DATA environment variable to
   the test data directory". The check sets `ARROW_TEST_DATA` and
   `PARQUET_TEST_DATA` and fails a milestone only on failures the base tree does
   not already have, so those two targets' state costs you nothing either way --
   read their output knowing what it is.

3. **The end-to-end pipeline** must still produce identical output, and must not
   get slower. `/app/tools/e2e.cc` reads a Parquet file and hash-joins it — the
   pipeline this kernel sits inside:

   ```bash
   /app/tools/build_probe.sh /app/arrow /app/build /app/tools/e2e.cc /tmp/e2e
   taskset -c 0 /tmp/e2e 3
   ```

   It prints `e2e_ms <float>` and `e2e_digest <hex>`. **The digest must be
   unchanged**, and the millisecond figure must not be more than **15%** above
   the ladder's base tree — the fully stripped tree rebuilt as its
   reference at every step, which is the tree you received at the first
   milestone. That is a pass/fail guard, not a speedup measurement.

## How your work is measured

Your kernel is timed **function by function**, by calling the implementation the
library actually installs — not by timing a whole read. A faster function
therefore shows up at its full value instead of being diluted by the rest of the
pipeline, and **every** function in this milestone is timed, so none of your
work goes unnoticed.

- One core, pinned (`taskset -c 0`). Threads cannot help.
- Each function is run in its own process repeatedly; the base tree and your
  tree are measured in alternating runs on the same core, and every sample from
  every run is pooled. The **median** of the pooled samples is used for each
  side. (A minimum was tried first and is worse at this sample budget: measured
  against two byte-identical binaries it drifts 45% on some functions, because
  the fastest of 175 samples says more about a lucky scheduling window than
  about the kernel.)
- `speedup = ns(function in the tree you inherited) / ns(function in yours)`,
  measured by the same harness, on the same sequence of inputs (the harness
  hands each timed call fresh input bytes, identically on both sides of the
  ratio, so a kernel that answers a repeated call from a cache measures the
  cache and not the kernel -- see `/app/tools/kbench.cc`, `Perturber`).
- With one function in scope, the measurement is simply that function's
  speedup: a function left at its inherited speed shows no improvement, and
  there is no other function whose gain could offset it.  (A tree that gets the
  whole pipeline slower is caught by the end-to-end guard below.)
- The harness also reports its own **physics check** on every measurement: for
  each function it compares the memory traffic the result implies (the fixture's
  bytes-per-unit estimate divided by the measured nanoseconds per unit) with
  this machine's own memcpy rate, measured in the same process. It is a
  diagnostic, printed into the measurement log and not part of the measurement --
  a function cannot outrun the memory system, so a verdict much above the
  machine's own memcpy rate is what a call hoisted out of the timed loop or an
  answer served from a cache looks like, and it is a sign your numbers are not
  the kernel's.

## The in-scope set, and how to enumerate it

The in-scope set is exactly the functions listed in `/app/SCOPE.md`. It is one
function: `parquet::BlockSplitBloomFilter::FindHash`.

Enumerate it from the harness itself — `all` selects exactly this milestone's
functions, and it prints one line per function, so you can see precisely how
much of the set you have covered:

```bash
taskset -c 0 /tmp/kbench samples 1 all        # one line per function in scope
taskset -c 0 /tmp/kbench samples 1 all | wc -l   # must print 1
```

A function with no optimized implementation is measured at 1.0, so "done" means the
line shows a ratio above 1.

## What is inside the measurement inputs

You are not told the draw, but you are told its shape, because a benchmark you
cannot reason about is a guessing game:

| function | unit of work | input |
| --- | --- | --- |
| `bloom.sbbf.find` | one probe | a split-block bloom filter sized for 16,384 distinct values at a 1% false-positive rate, with 16,384 hashes inserted; probed with 32,768 hashes, half present and half absent |

The measurement draw uses the same shapes at a seed that is not disclosed. Tuning a kernel
to one particular buffer buys nothing, because the buffer is a random draw.
