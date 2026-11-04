# Third-Party Notices — openscenario-engine-multiversion

Benchmark task that grows a C++17 ASAM OpenSCENARIO / OpenDRIVE scenario engine
across ten cumulative upstream esmini releases.

## Upstream project

| | |
|---|---|
| Project | esmini (Environment Simulator Minimalistic) |
| URL | https://github.com/esmini/esmini |
| Pins | base `v2.20.10`; targets `v2.23.1`, `v2.27.0`, `v2.27.5`, `v2.31.10`, `v2.44.2`, `v2.46.4`, `v2.51.1`, `v2.59.0`, `v3.0.4`, `v3.8.1` |
| Bundled/derived | The step-1 workspace is the full esmini v2.20.10 source tree (including its `3rd_party_terms_and_licenses/` directory). Each step's oracle patch (`steps/*/solution/`) is a benchmark-written transition derived from the corresponding upstream release. The step instructions, tests and handoff documents are task-authored. |
| Standards | ASAM OpenSCENARIO / OpenDRIVE XML schemas are used as the data model. The task ships no ASAM specification documents. |

## Version map

| Step | Source release | Target release |
|---|---|---|
| 01-lane-geometry-queries | `v2.20.10` | `v2.23.1` |
| 02-parameter-distributions | `v2.23.1` | `v2.27.0` |
| 03-scenario-variables | `v2.27.0` | `v2.27.5` |
| 04-bounding-box-and-alignment | `v2.27.5` | `v2.31.10` |
| 05-option-registry | `v2.31.10` | `v2.44.2` |
| 06-motion-state-queries | `v2.44.2` | `v2.46.4` |
| 07-drivable-lane-queries | `v2.46.4` | `v2.51.1` |
| 08-positional-scenario-arg | `v2.51.1` | `v2.59.0` |
| 09-double-precision-abi | `v2.59.0` | `v3.0.4` |
| 10-geo-and-collision-queries | `v3.0.4` | `v3.8.1` |

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| MPL-2.0 | `MPL-2.0.txt` | esmini itself, all pinned releases. Exact upstream `LICENSE`, copied from `steps/01-lane-geometry-queries/workdir/LICENSE`. |
| OSGPL-1.0 | `OSGPL-1.0.txt` | OpenSceneGraph. Exact upstream third-party terms file at `steps/01-lane-geometry-queries/workdir/3rd_party_terms_and_licenses/OpenSceneGraph_LICENSE.txt`. |
| EPL-2.0 | `EPL-2.0.txt` | Eclipse SUMO. Exact upstream third-party terms file at `steps/01-lane-geometry-queries/workdir/3rd_party_terms_and_licenses/sumo_LICENSE.txt`. |
| MIT | `MIT-fmt.txt`, `MIT-spdlog.txt` | fmt and spdlog sources vendored under `environment/vendor/`. |
| (no SPDX ID) | `FBX-Terms.txt` | Autodesk FBX code terms, reproduced verbatim from `3rd_party_terms_and_licenses/FBX_terms.txt`. |

**MPL-2.0 and Exhibit B.** `LICENSES/MPL-2.0.txt` is the complete Mozilla
Public License 2.0 text, including Exhibit A ("Source Code Form License
Notice") and Exhibit B ("Incompatible With Secondary Licenses Notice"). The
esmini source headers carry the Exhibit A notice ("This Source Code Form is
subject to the terms of the Mozilla Public License, v. 2.0..."). No esmini
source file in the pinned tree carries the Exhibit B declaration.

## Modifications made by the benchmark

- `steps/01-lane-geometry-queries/workdir/` is the upstream v2.20.10 tree.
  The benchmark adds only its own `PROFILE.md`, `TEST_INTERFACE.md`,
  `WORKSPACE_HANDOFF.md` and `timer.sh`; upstream source and the
  `3rd_party_terms_and_licenses/` files are unmodified.
- The oracle patches in `steps/*/solution/` are benchmark-written upgrades
  derived from the upstream releases in the version map; they are not upstream
  patch files.
- Third-party submodules that upstream fetches (`externals/osg`, `osi`, `proj`,
  `sumo`, `googletest`, `implot`) are not shipped in this repository; the
  environment excludes them from the collected artifact as well.

## Third-party dependencies bundled in the task

| Component | Version | License | Bundled path |
|---|---|---|---|
| OpenSceneGraph | as pinned by esmini v2.20.10 (not shipped as source) | OSGPL-1.0 | `3rd_party_terms_and_licenses/OpenSceneGraph_LICENSE.txt` in the step-1 workdir |
| Eclipse SUMO | as referenced by esmini (not shipped as source) | EPL-2.0 | `3rd_party_terms_and_licenses/sumo_LICENSE.txt` |
| Open Simulation Interface (OSI) | as pinned by esmini (not shipped as source) | MPL-2.0 | `3rd_party_terms_and_licenses/OpenSimulationInterface-OSI_LICENSE.txt` |
| Protocol Buffers | as pinned by esmini (not shipped as source) | BSD-3-Clause | `3rd_party_terms_and_licenses/Protobuf_LICENSE.txt` |
| GoogleTest | as pinned by esmini (not shipped as source) | BSD-3-Clause | `3rd_party_terms_and_licenses/googletest_LICENSE.txt` |
| Clothoids | as pinned by esmini | BSD-2-Clause | `3rd_party_terms_and_licenses/clothoids_LICENSE.txt` |
| tronkko/dirent | as pinned by esmini | MIT | `3rd_party_terms_and_licenses/dirent_LICENSE.txt`; source under `externals/dirent/` |
| expr | as pinned by esmini | MIT | `3rd_party_terms_and_licenses/expr_LICENSE.txt`; source under `externals/expr/` |
| odrSpiral | as pinned by esmini | BSD-style (VIRES) | `3rd_party_terms_and_licenses/odrSpiral_LICENCE.txt` |
| pugixml | as pinned by esmini | MIT | `3rd_party_terms_and_licenses/pugixml_LICENSE.txt`; source under `externals/pugixml/` |
| fmt | commit `40626af88bd7df9a5fb80be7b25ac85b122d6c21` | MIT, Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors | `environment/vendor/fmt/LICENSE`, `LICENSES/MIT-fmt.txt` |
| spdlog | commit `e593f6695c6065e6b345fe2862f04a519ed484e0` | MIT, Copyright (c) 2016 Gabi Melman | `environment/vendor/spdlog/LICENSE`, `LICENSES/MIT-spdlog.txt` |

## FBX terms — factual note

`LICENSES/FBX-Terms.txt` reproduces, verbatim, esmini's
`3rd_party_terms_and_licenses/FBX_terms.txt`:

> This software contains Autodesk® FBX® code developed by Autodesk, Inc.
> Copyright 2018 Autodesk, Inc. All rights, reserved. Such code is provided
> "as is" and Autodesk, Inc. disclaims any and all warranties, whether express
> or implied, including without limitation the implied warranties of
> merchantability, fitness for a particular purpose or non-infringement of
> third party rights. In no event shall Autodesk, Inc. be liable for any
> direct, indirect, incidental, special, exemplary, or consequential damages
> (including, but not limited to, procurement of substitute goods or services;
> loss of use, data, or profits; or business interruption) however caused and
> on any theory of liability, whether in contract, strict liability, or tort
> (including negligence or otherwise) arising in any way out of such code.

The FBX terms are an Autodesk notice and disclaimer, not an OSI-approved or
SPDX-listed license. They are included because esmini's distribution carries
them; they are not the terms of the esmini source itself.

## Required notices

- esmini: `Copyright (c) partners of Simulation Scenarios`, as stated in each
  source file header. No upstream `NOTICE` file applies.
- OpenSceneGraph Public License copyright: `Copyright (C) 2018 Robert Osfield`,
  as carried in `LICENSES/OSGPL-1.0.txt`.
- Eclipse SUMO EPL-2.0 text carries no single copyright line in the license
  file; SUMO's own source files carry their project copyrights.
- MIT components: see the copyright lines in `LICENSES/MIT-fmt.txt` and
  `LICENSES/MIT-spdlog.txt`.
- Autodesk FBX: `Copyright 2018 Autodesk, Inc.`; see the full terms above.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
