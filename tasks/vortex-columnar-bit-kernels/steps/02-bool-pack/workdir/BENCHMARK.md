# How this milestone is built, checked and measured

## Build

```sh
cd /app/vortex
cargo build --release -p vortex-buffer -p vortex-mask -p vortex-array
```

The tree is pre-warmed, so an incremental build is seconds. Do not add
dependencies: the verifier builds offline against the registry this tree shipped
with.

## What "behaviour must not change" means, as runnable commands

The project's own suite, which the tree you started from does not pass clean:

```sh
cd /app/vortex
cargo test --release -p vortex-buffer -p vortex-mask -p vortex-array --lib
```

The verifier runs the same command and compares the set of failing tests against
the one it recorded for the base tree when the verifier image was built. **No
new failures** is the gate; the baseline's own failures are not part of the
requirement.

Beyond that, every in-scope entry point is called on freshly drawn inputs in both
trees and its output is hashed. A single differing bit anywhere means this
milestone is not satisfied, however fast the tree is. A realistic
filter-then-take scan over a Vortex array is measured end to end as well. Its
result must be identical to the baseline's, and its time is bounded: a scan
that runs more than 3x slower than the baseline's means this milestone is not
satisfied, however good the per-function rows are. The bound is deliberately loose -- the
scan is the noisiest row the verifier measures -- so it is a backstop against
buying the per-function rows at the pipeline's expense, not a target.

## How speed is measured

Per function, not per program.

For each in-scope entry point below, the verifier calls it in a build of the base
tree — the same base at every step, built in this container — and in a build of
your tree, in the same container, on the same pinned core, on **identical
inputs**:

```
speedup_f = time_baseline(f) / time_yours(f)
```

This milestone is measured by those per-function ratios, and every output must
be bit-identical to the baseline's.

Three consequences worth knowing before you start:

- **`ns` per call, lower is better.** Each row is the median of 9 samples, and
  **each sample draws a fresh input**, so a result cached between calls is
  measured as a cache hit rather than as work.
- **A row you leave alone stays at `1.00x`.** A row that gets slower is floored
  at `1.00x`, so a regression loses that row's win but cannot pull the
  measurement below the untouched value.
- **The draw is not disclosed.** The predicate, the bit pattern it produces and
  the seed belong to the verifier. Optimize the general case; an implementation
  that only wins at one shape does not generalise.

## The in-scope set

Two entry points, each at two lengths:

```
collect_bool_words_mv@8192
collect_bool_words_mv@65536
bitbuffer_collect_bool_mv@8192
bitbuffer_collect_bool_mv@65536
```

Both keys are the `*_multiversioned` entry points: `@65536` and `@8192` are
the number of bits packed. The predicate and the bit pattern are drawn per
sample and are not disclosed.

**Done means every row above is faster than the baseline.** All 4 rows are in
scope.

## Checking yourself while you work

The project ships a benchmark for this stage that registers the dispatched loop
and the portable kernels beside it:

```sh
cd /app/vortex
cargo bench -p vortex-buffer --bench collect_bool
```

It is not the measurement workload — different lengths, and it repeats one input
rather than redrawing per sample — and it does not cover this milestone:
`words_gather_dispatch` times `collect_bool_words`, the compile-time tier this
step leaves alone, and `pack_words_swar` times the portable pack kernel rather
than a word loop. Nothing in that group isolates
`collect_bool_words_multiversioned`: leave the compile-time tier alone, as this
step asks, and those numbers stay where they are.

A micro-benchmark of your own is nearer to what the verifier does and takes a
minute to write: add a `benches/` target to `vortex-buffer` (a `[[bench]]` entry
with `harness = false` in its `Cargo.toml`, a `fn main()` in the file) that
builds a bool source of each in-scope length and times
`vortex_buffer::collect_bool_words_multiversioned` — or
`BitBuffer::collect_bool_multiversioned` — in a loop, taking the median of
several runs rather than the best.
