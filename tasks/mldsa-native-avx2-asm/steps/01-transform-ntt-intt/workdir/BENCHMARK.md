# How to check your work

## Build

```bash
cd /app/mldsa-native
make lib OPT=1 AUTO=1 BUILD_DIR=/app/build          # the static libraries
make func_65 unit_65 kat_65 OPT=1 AUTO=1 BUILD_DIR=/app/build
```

`OPT=1 AUTO=1` is the project's own optimized configuration; leave them on.
The `AUTO` flag adds this host's `-mavx2 -mbmi2`, which is the ceiling.

## Behaviour must not change

The library has to produce exactly the bytes it produced before, for every
input and every parameter set. All of these are re-run, on your tree, when your
work is checked; each is one of the project's own suites, and a suite this
milestone's reference tree already fails is out of scope.

```bash
cd /app/mldsa-native
make run_func_65 run_func_44 run_func_87 OPT=1 AUTO=1 BUILD_DIR=/app/build
make run_unit_65 run_unit_44 run_unit_87 OPT=1 AUTO=1 BUILD_DIR=/app/build   # differential: native vs C
make run_kat_65  run_kat_44  run_kat_87  OPT=1 AUTO=1 BUILD_DIR=/app/build   # published KAT digests
```

`run_unit_*` is the strongest of the three: it computes every optimized entry
point twice, once through the implementation the library installs and once
through the portable C reference, and requires the two to agree bit for bit.

The functions in this milestone are also compared directly, entry point by
entry point, against a build of the tree you were handed — not through the
whole codec. See "How your work is timed" below.

## How your work is timed

Your milestone's entry points are timed **individually**, by calling the
library's own entry point (the one that dispatches to the installed
implementation) in both trees, on identical freshly generated inputs — a fresh
draw for every timed call, so no timed call repeats an input. Nothing
is diluted by the rest of the pipeline, and every entry point this milestone
covers is timed, so none of your work goes unnoticed. One ratio per
(entry point, parameter set) pair; each ratio is measured on its own.

The pairs this milestone covers:

- `poly_ntt` (ML-DSA-44, ML-DSA-65, ML-DSA-87)
- `poly_invntt_tomont` (ML-DSA-44, ML-DSA-65, ML-DSA-87)

This milestone covers **6 pairs** — the whole list above. A pair left
unoptimized reads 1.0x, so the target is every pair in the list, not the
first one that moves.

There is also an **end-to-end guard**: keypair, sign and verify must not come
out more than 3x slower than a build of this tree with every optimized stage
removed. That bound is deliberately coarse — this target host is shared and
two builds of byte-identical source have been measured 1.7x apart on it, so a
tighter bound would fire on noise. It is there to catch a microbenchmark won
at the pipeline's expense, not to be treated as a target.

The project ships its own component benchmark, which times many of the same
entry points and is the closest thing to a local view of the above:

```bash
cd /app/mldsa-native
make run_bench_components_65 CYCLES=PMU OPT=1 AUTO=1 BUILD_DIR=/app/build
```

**The measurement inputs are not disclosed.** They are generated deterministically
inside the checker from its own seed, so they are identical in every run but not
visible here. Optimize the entry points for the general case rather than for
one input.

`CYCLES=PMU` means the project's own counter, which is `rdtsc` on x86-64; the
checker uses the same counter, compiled from its own reference copy of the tree
rather than from yours. Pin to a core (`taskset -c N`) and treat a single
reading as indicative only: the checker pins, runs the measurement five times
interleaved with the reference, and keeps the best of the five readings on each
side.
