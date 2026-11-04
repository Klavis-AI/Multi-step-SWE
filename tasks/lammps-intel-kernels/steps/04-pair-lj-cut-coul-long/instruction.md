# Milestone: make LAMMPS's Lennard-Jones with long-range Coulomb pair style fast

## 1. What this milestone is

`/app/lammps` is LAMMPS at a pinned upstream tag with the accelerated twin of
each Intel-package style removed from `src/INTEL/`. The library is complete and
correct: it builds, and every benchmark input in `src/INTEL/TEST/` runs to
completion on the reference C++ styles. It is simply slow where the accelerated
twin is missing. The other accelerator packages (`KOKKOS`, `OPENMP`, `GPU`,
`OPT`) and the `lib/gpu` kernel library are not part of this tree either, so
some of the project's own documentation refers to packages that are not here.

**This milestone: make the `lj/cut/coul/long` pair style faster, without changing the
physics it computes.**

`lj/cut/coul/long` — the direct sibling of `lj/cut` with a real-space Coulomb term and a particle-particle particle-mesh mesh solve behind it. The tree currently has no accelerated
implementation of it: `pair_lj_cut_coul_long_intel.{cpp,h}` is not in the tree, so `lj/cut/coul/long/intel` does not resolve and LAMMPS falls back to the reference C++ kernel.

You are measured on the implementation the library **installs** for this style,
which is how LAMMPS itself selects an accelerated style — the style name with
the `intel` suffix (`-sf intel` on the command line, or `pair_style
lj/cut/coul/long/intel` in an input script). So deliver your work behind that name. How
you write it is up to you: assembly, intrinsics, or restructuring the kernel in
C++. A style you optimise in place under its plain name, without an `intel`
twin, is not measured — the comparison is against the reference
implementation, so it would show no difference.

Earlier milestones must keep working. This workspace is cumulative: whatever
previous milestones added is still here and is still part of this milestone's
baseline, so a regression fails this milestone as much as the earlier one.

## 2. What success means

1. **The physics must not change.** This is an optimisation task, not a
   behaviour-change task. Running an input with the style suffix on and off must
   produce the same thermodynamic trajectory over the compared window — the
   first 25 printed timesteps of the run — to within the budget `BENCHMARK.md`
   states, which is the budget the measurement applies. The reference path must keep
   behaving as it does now and must not be made slower. An approximation, a
   dropped or reordered interaction, a cutoff that moved, or a fast path that is
   right "almost always" is a failure, however fast it is. `/app/BENCHMARK.md`
   gives the window, the budget and the commands.
2. **This style must get faster**, on a workload you cannot see.
   `/app/BENCHMARK.md` gives the exact measurement — the build, the numbers
   taken from it, and the arithmetic — so you can confirm where you stand. Only
   this style is in scope; making some other part of LAMMPS faster does not
   satisfy this milestone.

## 3. Where to start reading

- `src/INTEL/` — the accelerated package. `intel_buffers.{cpp,h}` is the
  packed atom buffer (`IntelBuffers<flt_t, acc_t>`); `fix_intel.{cpp,h}` is
  the fix that owns it and hands it to a style (`get_buffern`,
  `get_double_buffers`, `add_result_array`, the pack width, the three-body
  neighbour flag); `intel_simd.h`, `intel_intrinsics.h` and
  `intel_preprocess.h` are the vectorisation helper layer, including the
  `IP_PRE_*` macros a kernel uses to reach the buffers and to split the loop.
  All of that is present and is yours to use — read the headers for the
  exact signatures.
- `src/KSPACE/pair_lj_cut_coul_long.cpp` — the reference implementation of this style. It defines the
  correct output; do not edit it.
- `src/INTEL/README` and `doc/src/Speed_intel.rst` — how the package is
  documented upstream.
- `src/INTEL/TEST/in.intel.lj` and the rest of `src/INTEL/TEST/` — the benchmarks
  this package ships. Development inputs, not the measurement workload.
- `/app/lammps/cmake/presets/basic.cmake` — how the build is configured.
- `/app/BENCHMARK.md` — build, behaviour checks, measurement, envelope.
- `/app/WORKSPACE_HANDOFF.md` — the state you inherited.

## 4. Meta requirements

- The implementation budget is 14,400 seconds. `bash /app/timer.sh` prints the
  time left; the configured timeout is authoritative and resets for this
  milestone.
- Work in the cumulative `/app` workspace. Keep `/app/lammps` as the source
  tree and keep the build working with the flags in `BENCHMARK.md`.
- **Do not edit the tree you were given; add to it.** Every file that is
  already in `/app/lammps` must stay byte-identical — the reference
  implementation, and the package's shared helper and neighbour-list layers
  (`fix_intel.*`, `intel_buffers.*`, `intel_preprocess.h`, `npair_*_intel`,
  `nbin_intel`, `nstencil_*_intel`) included — and new compiled sources belong
  under `src/INTEL/`. This milestone is measured by what it adds: your tree is
  compared against the tree you were handed, and any other change fails the
  milestone. `/app/BENCHMARK.md` states the rule in full.
- **This style is measured on AVX2 with BMI2.** The reference implementation this
  is compared against is built for that target, so a candidate at the ceiling is
  its peer. AVX-512 is not executable in this environment, so it must not reach
  the compiled code: the compiled objects of the files this milestone owns must
  contain no 512-bit registers. `BENCHMARK.md` states the check in full.
- Do not weaken, filter, rename or disable anything under the tree's own test
  material.
- This environment has no network access. **Do not fetch, copy, vendor or
  transcribe upstream LAMMPS's own accelerated implementation of this style, or
  any other project's, from the internet, a package mirror or a vendor SDK
  sample.** The implementation must be yours. Do not inspect `/tests`, later
  milestones' directories, or any solution material.
- Leave `/app/WORKSPACE_HANDOFF.md` updated: what you changed, the speedup you
  measured, and how to rebuild.
