# Workspace handoff

This milestone runs in the same `/app` worktree as the last one, but may begin
a brand-new conversation with no memory of it. **The source tree in
`/app/lammps` is your starting checkpoint** -- whatever the previous milestone
left. Do not reset it or undo work already there: earlier work is re-checked
for correctness here.

## Your target this milestone

short-range Lennard-Jones pair style. The tree has no accelerated implementation of `lj/cut`: `pair_lj_cut_intel.{cpp,h}` is not in the tree, so `lj/cut/intel` does not resolve and LAMMPS falls back to the reference C++ kernel.

You may implement it by any route -- assembly in a new file under
`src/INTEL/`, intrinsics, or a restructured C++ kernel. LAMMPS selects an
accelerated style by name, so deliver it as `<style>/intel`; that is the name
the measurement resolves. The measurement times the implementation the
library installs behind that name.

Other parts of the tree may already be accelerated, or may still be on their
reference paths. Neither is your concern here: only `lj/cut` is in scope.

## What is in the workspace

- `/app/lammps` -- the source tree, complete and working.
- `/app/lammps/src/INTEL/` -- the accelerated package; the packed-buffer and
  SIMD helper layers are present and are yours to use.
- `/app/lammps/src/INTEL/TEST/` -- the benchmarks this package ships.
  Development inputs, not the measurement set.
- `/app/BENCHMARK.md` -- how to build, check behaviour, and time your work.
- `/app/tools/rowtime.sh` -- runs an input both ways and prints the ratio.
- `/app/timer.sh` -- advisory countdown.

## Step log

Append what you changed and the speed difference you measured.
