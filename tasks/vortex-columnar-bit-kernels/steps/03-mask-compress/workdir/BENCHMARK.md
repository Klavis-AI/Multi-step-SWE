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
- **The draw is not disclosed.** Lengths, densities, bit patterns and seeds
  belong to the verifier. Optimize the general case; an implementation that only
  wins at one shape does not generalise.

## The in-scope set

One entry point, at four element widths:

```
filter_i8
filter_i16
filter_i32
filter_i64
```

Each key filters an array of that element type under masks drawn per sample.
The array length and the mask density are the verifier's and are not disclosed;
every mask used is dense enough and long enough to be eligible.

**Done means every row above is faster than the baseline.** All 4 rows are in
scope.

## Checking yourself while you work

The project ships a benchmark for this stage:

```sh
cd /app/vortex
cargo bench -p vortex-array --bench filter_fixed_width
```

`random_i8` through `random_i64` cover the same four widths. It is not the
measurement workload — different lengths, fixed densities, and one input repeated —
but it is a fair reading of whether the kernel you installed is faster than the
one it replaced.
