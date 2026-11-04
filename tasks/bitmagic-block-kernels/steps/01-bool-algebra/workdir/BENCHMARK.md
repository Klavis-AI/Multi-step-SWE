# How to check your work — milestone 01

## The in-scope set

**17 entry points in this milestone, and this is the whole list:**

```
and
and_digest
and_2way
and_3way
and_5way
init_and_2way
xor
xor_2way
sub
sub_digest
sub_2way
sub_3way
sub_5way
or_2way
or_3way
block_init_digest0
update_block_digest0
```

`/app/tools/bench_kernels list` prints the same list. Every one of them is
`bm::` free function; the signatures are in the instruction.

An entry point with no optimized implementation is measured at a speedup of
1.0, so "done" means the named set is covered — you can enumerate it and check
your own coverage. Nothing outside this list is measured: work elsewhere in the
library has no effect either way.

## Build

```bash
cmake -S /app/bitmagic -B /app/build -DCMAKE_BUILD_TYPE=Release -DBMOPTFLAGS=BMAVX2OPT
cmake --build /app/build -j"$(nproc)" --target bmtest
```

The measurement harness is header-only and compiled separately:

```bash
bash /app/tools/build.sh          # -> /app/tools/bench_kernels, /app/tools/e2e_bench
```

## Behaviour must not change

The library has to produce exactly the bytes it produced before. All of these
are re-run, with fresh inputs, when your work is checked.

1. **The entry points themselves**, compared byte for byte against the
   unoptimized base tree the task starts from (the same baseline at every
   milestone, not the cumulative tree you inherited). The harness dumps every
   observable output of one call — the destination block and the entry's own
   result. That result is compared byte for byte — except for `and` and
   `xor_2way`, which return a whole-block zero-predicate and are compared
   set/not-set (a nonzero result must stay nonzero, a zero result must stay
   zero), which is all their contract defines:

   ```bash
   for v in 0 1 2; do /app/tools/bench_kernels <entry-id> 0 $v dump | md5sum; done
   ```

   The harness also replays the timed sequence itself — the same calls, on the
   same draws, in the same order `bench` times — and prints every output of every
   call in it, compared the same way. The calls that are timed are checked, not
   only the one call the dump above prints:

   ```bash
   for v in 0 1 2; do /app/tools/bench_kernels <entry-id> 0 $v trace 5 | md5sum; done
   ```

   (seed `0` means the harness's own built-in seed, so two runs of the same
   command are comparable.) Three variants are drawn per entry: `0` dense random
   words, `1` sparse, `2` word runs. Compare against the same commands run on the
   tree you started from. This is equality, not a tolerance. The comparison
   runs over every entry in scope, on two input seeds that are not disclosed.
2. **The whole pipeline**, through the public API — a composite of `bit_and`,
   `bit_or`, `bit_xor`, `bit_sub`, `count`, `optimize`, the aggregator's fused
   forms, `set_bit_conditional`, `count_range`, `find`, and serialization and
   deserialization round trips over a generated corpus:

   ```bash
   /app/tools/e2e_bench digest        # one hex number; it must not change
   ```

   Serialization depends on representation choice, on digests and on GAP
   encoding, so this is a sharp end-to-end check on the block layer, and it is
   the check that catches damage outside the entry points you were asked to
   optimize.
3. **The project's own build system must still configure:**

   ```bash
   cmake -S /app/bitmagic -B /app/build -DCMAKE_BUILD_TYPE=Release -DBMOPTFLAGS=BMAVX2OPT
   ```
   (`tests/stress/t.cpp`, the project's randomised soak test, is deliberately
   *out of scope*: it spends more than half an hour inside a single
   search-family case and never reaches the block-layer tests this milestone
   touches. The composite in item 2 is the collateral-damage check instead.)

## How your work is timed

Your entry points are timed **individually**, by calling the implementation the
library installs for each one — not by timing a whole bvector operation. So a
faster kernel shows up at its full value instead of being diluted by the rest of
the pipeline, and every entry in the in-scope set is timed, so none of your work
goes unnoticed.

```bash
/app/tools/bench_kernels <entry-id> <seed> <variant> bench [reps]
```

prints one number: nanoseconds per call, the median of `reps` batches. Lower is
better. The harness rotates over 32 freshly-restored destination blocks per
batch, redraws the batch's inputs before the clock starts (so no input is ever
timed twice), and charges only the call to the number, so the figure is the
kernel's own cost and not the block-copy around it. It pins nothing itself — run it under
`taskset -c 0,1` on an otherwise idle machine, as the timed run does.

**The measured inputs are not disclosed** — seeds and the draw they produce are
not published. The *family* is disclosed: every entry in scope is measured on a
dense, a sparse and a run-structured block, so nothing is measured on a single
density. Do not try to infer the draw; optimize the general case\.

There is also an **end-to-end non-regression guard**: the composite above must
not get more than **60%** slower than the tree you inherited. The threshold is
deliberately very loose because the run shares a host with other work and a
whole-program timing moves by tens of percent; the guard is there to catch a
pipeline that has genuinely regressed, which is a multiple, not a few percent.

```bash
/app/tools/e2e_bench bench          # milliseconds for the whole composite
```

Treat your own numbers as indicative. **The final measurement is the
authoritative one**, and it interleaves the two builds so host load cancels.

