# Third-Party Notices — lammps-intel-kernels

A five-step benchmark ladder in which a candidate restores LAMMPS's accelerated
`INTEL` package pair styles (`lj/cut`, `sw`, `tersoff`,
`lj/cut/coul/long` and `airebo`) in a tree from which those accelerated styles
have been removed.

## Upstream project

| | |
|---|---|
| Project | LAMMPS (Large-scale Atomic/Molecular Massively Parallel Simulator) |
| Repository | https://github.com/lammps/lammps |
| Pin | tag `stable_22Jul2025_update6`, commit `9c5ab448c78a14fd534619622162ba418d6a1fb1` |
| Upstream license | GPL-2.0 (upstream `LICENSE`) |

The task repository does not vendor the LAMMPS source tree. The agent image and
every verifier image clone the pinned commit at build time and remove the
`src/INTEL` accelerated styles with `environment/vendor/strip.py`.

### Bundled and derived material

* `steps/*/solution/oracle.patch` — five patches derived from upstream
  `src/INTEL/` sources. They add the accelerated `pair_*_intel.cpp/.h` files and
  their build/dispatch wiring for the five ladder stages. Files carried over
  from upstream retain their upstream copyright and GPL headers.
* `environment/vendor/` and each step's `tests/vendor/` — benchmark-authored
  stripping, input-generation and timing tooling (`strip.py`, `make_inputs.sh`,
  `record_base.sh`, `diff_scope.py`, `rowtime.sh`).
* `steps/*/tests/IN.*` — benchmark-authored LAMMPS input decks for the scored
  workloads.
* No upstream source is committed to the task repository.

## Licenses

| SPDX ID | File under `LICENSES/` | Applies to |
|---|---|---|
| GPL-2.0 | `GPL-2.0.txt` | LAMMPS stable_22Jul2025_update6. Verbatim upstream `LICENSE` (GNU General Public License version 2). |

## Modifications made by the benchmark

* The starting tree is the upstream pin with `src/INTEL` accelerated style
  families removed, together with `src/KOKKOS`, `src/OPENMP`, `src/GPU`,
  `src/OPT` and `lib/gpu` (alternative implementations of the same kernels).
  The reference C++ styles and the Intel package infrastructure remain.
* Verifier images rebuild the identically stripped base and measure against it;
  the oracle patches restore the upstream accelerated styles.
* No behavioural change is made to the remaining upstream code.

## Third-party dependencies bundled in the upstream tree

The task repository itself contains no third-party code beyond the derived
LAMMPS patches. The full LAMMPS tree fetched at image-build time contains
optional libraries under `lib/` and `src/` with their own in-tree license files;
they are unmodified by this task:

| Component | In-tree path | License (per in-tree file) |
|---|---|---|
| Kokkos | `lib/kokkos/` | Apache-2.0 WITH LLVM-exception (`lib/kokkos/LICENSE`, `LICENSE_FILE_HEADER`) |
| h5md | `lib/h5md/` | BSD-3-Clause (`lib/h5md/LICENSE`) |
| Lepton | `lib/lepton/` | MIT (`lib/lepton/LICENSE`) |
| AsmJit | `lib/lepton/asmjit/` | Zlib (`lib/lepton/asmjit/LICENSE.md`) |
| AWPMD | `lib/awpmd/` | GPL-2.0 (`license/COPYING`) plus the AWPMD library license v1 and wxWidgets license in `license/` |
| POEMS | `lib/poems/` | POEMS license (`lib/poems/POEMS_License.txt`, `Copyright_Notice`) |
| PTM | `src/PTM/` | MIT (`src/PTM/LICENSE`) |
| fmt | `src/fmt/` | MIT (`src/fmt/LICENSE`) |
| Read the Docs Sphinx theme | `doc/utils/sphinx-config/_themes/lammps_theme/` | MIT (`LICENSE`); the directory also carries Apache-2.0 and OFL font license texts |

`lib/gpu` (CUDA/OpenCL kernels, BSD-3-Clause) is deleted by the benchmark
stripper and is not shipped.

## Required notices

The restored `src/INTEL` files carry the upstream LAMMPS header:

```
LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
https://www.lammps.org/, Sandia National Laboratories
LAMMPS development team: developers@lammps.org

Copyright (2003) Sandia Corporation.  Under the terms of Contract
DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
certain rights in this software.  This software is distributed under
the GNU General Public License.

See the README file in the top-level LAMMPS directory.
```

LAMMPS is a trademark of Sandia National Laboratories.

---

Task-authored benchmark content is licensed separately by the benchmark maintainers.
