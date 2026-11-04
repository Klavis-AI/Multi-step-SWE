# How to build, check and time your work

## Build

The tree is a LAMMPS checkout whose `src/INTEL/` accelerated styles have been
removed. It builds with the Intel compiler that ships in this image:

```bash
cmake -S /app/lammps/cmake -B /app/build \
      -C /app/lammps/cmake/presets/basic.cmake \
      -D PKG_INTEL=on -D PKG_ML-SNAP=on -D PKG_ASPHERE=on -D PKG_DPD-BASIC=on \
      -D BUILD_MPI=no -D BUILD_OMP=on -D CMAKE_BUILD_TYPE=Release \
      -D CMAKE_CXX_FLAGS="-mavx2 -mfma -mbmi -mbmi2 -O3" \
      -D CMAKE_C_FLAGS="-mavx2 -mfma -mbmi -mbmi2 -O3"
cmake --build /app/build -j4
```

Then `src/INTEL/` sources are picked up automatically: the build globs
`src/INTEL/*.cpp`, so a new file you add needs no registration and no edit to any
build file. (`cmake/Modules/Packages/INTEL.cmake` also *names* the package's own
sources in a hard-coded list, which is kept in step with the tree as shipped:
naming a file that is not present makes configure fail, so leave it alone — and
editing a shipped file fails the diff-scope rule below anyway.)

**Use the Intel compiler.** The package's vectorisation machinery is enabled
only for Intel compilers — see `src/INTEL/intel_preprocess.h`, where
`INTEL_VECTOR_WIDTH` stays at 1 for anything else. Built with `cc`, the
accelerated styles are *slower* than the reference. Both sides are built
with the flags above.

## Behaviour must not change

The physics must be exactly what it was. Concretely, all of these are enforced
when your work is checked, on inputs beyond the development set:

1. **The thermodynamic trajectory.** With the style suffix on and off, the same
   input must produce the same `temp`, `pe`, `ke`, `etotal` and `press` over the
   **first 25 printed timesteps** of the run, to within

       abs 1e-4 + 1e-7 x |value|

   on every one of those columns — the absolute-plus-relative form LAMMPS's own
   regression budget takes (`tools/regression-tests/config.yaml`), applied
   column by column to the columns the run prints. An
   approximation, a dropped interaction, or a cutoff that moved is a failure
   however fast it is.
   The comparison is bounded to the first 25 printed timesteps on purpose: a
   *correct* implementation that accumulates forces in a different order is
   still correct, and molecular dynamics amplifies a rounding-level difference
   exponentially, so an unbounded comparison would fail a correct tree.
2. **The reference path is frozen.** Whatever you add, the tree's *reference*
   styles must still behave — and time — exactly as they did. The measurement runs
   each input twice, once with the style suffix off and once with it on, and
   compares. Do not make anything faster by making the reference slower.
3. **Everything else in the tree is frozen too.** The measurement compares
   `/app/lammps` against the tree this milestone handed you and fails the
   milestone if any file that was already there was modified or deleted — the
   reference implementation, and the package's shared infrastructure
   (`fix_intel.*`, `intel_buffers.*`, `intel_preprocess.h`, the
   `npair_*_intel` / `nbin_intel` / `nstencil_*_intel` neighbour-list layer)
   alike. Deliver work as **new files under `src/INTEL/`**, which is where the
   build picks accelerated styles up from; a new *compiled* source file outside
   `src/INTEL/` fails the same comparison, because a change the milestone does
   not own is not attributable to the milestone that is being measured. Files
   you add that are not compiled (logs, dumps, your own notes, a CMake cache)
   are ignored.

## How speed is measured

For each style this milestone names, LAMMPS is run twice on the same input:
once with the style suffix off (the reference implementation) and once with
`-sf intel` (your implementation, if you wrote one — LAMMPS falls back to the
reference when it is absent, so a tree that adds nothing measures 1.0 rather
than crashing).

The number taken from each run is LAMMPS's own section timer — the row of the
`MPI task timing breakdown` table for the family this milestone covers
(`Pair` for a pair style). That row is the narrow timer around that family of
kernels, so nothing else in the timestep dilutes it. An input with more than one
`run` command prints one table per `run`, and the number is read from the
**last** table in the log — the final run, never a warm-up one.

Your speedup for a style is

```
    min(your reference row,                 <- the numerator, capped
        the pristine reference tree's row)
    ------------------------------------
    the accelerated run's row time          <- the denominator
```

so a smaller accelerated number is better, and the cap is what stops a slower
reference implementation from counting as progress.

```bash
# a run, reduced to the row this milestone covers:
/app/build/lmp -in in.intel.lj -log /tmp/a.log -screen none
/app/build/lmp -in in.intel.lj -log /tmp/b.log -screen none \
    -pk intel 0 omp 0 mode double -sf intel
grep -A3 'MPI task timing breakdown' /tmp/a.log | tail -4   # the Pair row, last table
```

Three things about the arithmetic, because you cannot reproduce them from a
single pair of runs:

- **Your own reference run is not the numerator on its own.** The
  numerator is `min(your reference row, the pristine reference tree's row)` —
  and the pristine row is re-measured in the measurement session, beside your own
  runs, not read from a figure recorded in advance — so slowing the reference
  implementation down buys nothing.
- **Runs alternate and the median is taken.** This machine is shared with other
  tenants; the two configurations are interleaved run by run and each side is
  reduced with a median, so that whichever side lands in a quiet window does not
  set the ratio.
- **Both sides run pinned, single-threaded** (`OMP_NUM_THREADS=1`,
  `package intel 0 omp 0`).

`/app/tools/rowtime.sh` does the measurement for you on an input of your
choosing — the same two configurations, the same last-table row time, the same
alternating runs and median reduction. The one thing it cannot reproduce is the
`min()` above, which needs the pristine tree; leave the reference path alone and
that `min()` is a no-op:

```bash
cd /app/lammps/src/INTEL/TEST && export LMP_ROOT=/app/lammps
/app/tools/rowtime.sh /app/build/lmp in.intel.lj Pair
```

There is also an end-to-end guard: the accelerated configuration as a whole
must not be more than **20 % slower** than the pristine tree's reference run on
the same input — that run is measured fresh in the same session,
alternately with your own — so a kernel that is fast in its own row while
pushing cost into packing, neighbour rebuilds or communication still fails. It is a coarse structural guard and it is stated as
one: this host is shared and noisy enough that a per-step timing comparison
carries a spread of roughly +-10-15 %, so the guard can see a kernel that moved
real cost out of its row and cannot see a few percent. It is not a fine-grained
budget, and you should not treat it as one.

## What is in scope, and how to enumerate it

This milestone names the style(s) it covers in its instruction. Each one
is a `/intel` style the tree does not currently define. A style with no
accelerated implementation contributes a speedup of 1.0,
so "done" means every style the milestone names is implemented **and** faster.
Every ratio matters on its own — a large win on one style cannot hide doing
nothing on another.

**The workload is not disclosed.** The measurement generates its own inputs at
sizes and step counts it does not publish; the benchmark inputs in
`src/INTEL/TEST/` are for development and are *not* the measurement set. Optimise
the general case: a kernel that is only fast at one system size, or that assumes a
configuration you happened to see, will not pay off.

## The instruction-set envelope

AVX2 with BMI2 is the ceiling, and it is a fairness rule rather than a
technicality: the reference implementation this is compared against is built for
the same target, so a candidate at the ceiling is its peer. This environment has
no AVX-512 and cannot execute it, so **AVX-512 must not reach the compiled code**
of the files this milestone owns: the objects its own build produced for those
files are disassembled, and the milestone fails if a 512-bit register (`zmm`)
appears in any of them. What is checked is the object, not the source
text: AVX-512 code that is guarded so that it never compiles — upstream's own
styles carry such blocks — is reported as a diagnostic and is not a failure.
