# Third-Party Notices: pdal-point-cloud-multiversion

## 1. Task

Seven cumulative steps over PDAL 2.0.1: implement each upstream release from
2.1.0 to 2.7.1, including its migrations, graded through the `pdal` CLI.

## 2. Upstream project and pins

| Upstream | URL | Pins | Bundled or derived in this task |
|---|---|---|---|
| PDAL | https://github.com/PDAL/PDAL | releases 2.0.1 through 2.7.1; the step workspaces target 2.1.0, 2.2.0, 2.3.0, 2.4.0, 2.5.0, 2.6.0 and 2.7.1 | Step 1's `workdir/` is a full PDAL 2.0.1 tree (including `vendor/`, `plugins/`, docs and `LICENSE.txt`). Steps 2-7 carry the target release's documentation subset under `workdir/spec/<version>/` (including that release's `LICENSE.txt`) and a benchmark-authored `upgrade.patch` that advances the cumulative tree. |
| laz-perf | https://github.com/hobuinc/laz-perf | 3.2.0 | `environment/assets/lazperf/` is a verbatim copy of `laz-perf-3.2.0/cpp/lazperf/` plus the upstream `COPYING`; the 2.5.0-level upgrade vendors this version. |

The full 2.0.1 tree and the target-release spec documents are stored in the
task. The 2.1.0-2.7.1 engine changes are carried as benchmark-authored patches
derived from the upstream releases; the upstream release tarballs are not
stored separately.

## 3. License map

| SPDX ID | File under `LICENSES/` | Component / version covered |
|---|---|---|
| BSD-3-Clause | `BSD-3-Clause-2019.txt` | PDAL 2.0.1-2.4.0; exact in-tree `steps/01-stage-cohort/workdir/LICENSE.txt` and `spec/2.1.0/LICENSE.txt`-`spec/2.4.0/LICENSE.txt`, Copyright (c) 2019 Hobu, Inc. |
| BSD-3-Clause | `BSD-3-Clause-2022.txt` | PDAL 2.5.0, 2.6.0 and 2.7.1; exact in-tree `spec/2.5.0/LICENSE.txt`, `spec/2.6.0/LICENSE.txt`, `spec/2.7.1/LICENSE.txt`, Copyright (c) 2022 Hobu, Inc. |
| Apache-2.0 | `Apache-2.0-lazperf.txt` | laz-perf 3.2.0; exact in-tree `environment/assets/lazperf/COPYING` |
| MIT | `MIT-arbiter.txt` | arbiter 1.13; exact in-tree `vendor/arbiter/LICENSE`, Copyright (c) 2015 Connor Manning |
| BSD-3-Clause | `BSD-3-Clause-gtest.txt` | Google Test 1.8.0; exact in-tree `vendor/gtest/LICENSE`, Copyright 2008 Google Inc. |
| MPL-2.0 | `MPL-2.0.txt` | Eigen 3.3.4; exact upstream `COPYING.MPL2` from the Eigen 3.3.4 tag |
| BSD-3-Clause | `BSD-3-Clause-kazhdan.txt` | Kazhdan/Bolitho Poisson reconstruction code in `vendor/kazhdan/MemoryUsage.h`; exact copyright and licence block from the file header, Copyright (c) 2006 Michael Kazhdan and Matthew Bolitho |
| MIT | `MIT-nlohmann.txt` | nlohmann/json 3.6.1; exact upstream `LICENSE.MIT`, Copyright (c) 2013-2019 Niels Lohmann |
| BSD-3-Clause | `BSD-3-Clause-nanoflann.txt` | nanoflann 1.2.3; exact upstream `COPYING`, Copyright 2008-2011 Marius Muja, David G. Lowe and Jose L. Blanco |
| BSL-1.0 | `BSL-1.0.txt` | Boost 1.60.0 (`vendor/pdalboost`) and libE57Format 2.0.1 (`plugins/e57/libE57Format`); the upstream Boost 1.60.0 `LICENSE_1_0.txt` and the libE57Format `LICENSE.md` are byte-identical |
| BSD-3-Clause | `BSD-3-Clause-CRCpp.txt` | CRC++ in `plugins/e57/libE57Format/contrib/CRCpp/LICENSE`, Copyright (c) 2016 Daniel Bahr |

## 4. Benchmark modifications

- The step-1 workspace is the PDAL 2.0.1 tree; its content is upstream except
  for the benchmark's `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md`, `timer.sh`
  and `spec/` overlay.
- Each later step overlays that release's documentation subset under
  `workdir/spec/<version>/` and applies a benchmark-authored `upgrade.patch`
  derived from the upstream release changes.
- `environment/assets/lazperf/` copies laz-perf 3.2.0 verbatim; the 2.5.0-level
  patch vendors it, including the upstream `COPYING`.
- The solution patches are authored for the benchmark and are not upstream
  source files.

## 5. Bundled third-party dependencies

| Name | Version | License | Bundled path |
|---|---|---|---|
| arbiter | 1.13 | MIT | `steps/01-stage-cohort/workdir/vendor/arbiter/` |
| Google Test | 1.8.0 | BSD-3-Clause | `steps/01-stage-cohort/workdir/vendor/gtest/` |
| Eigen | 3.3.4 | MPL-2.0 | `steps/01-stage-cohort/workdir/vendor/eigen/` |
| Kazhdan/Bolitho reconstruction code | vendored at 2.0.1 | BSD-3-Clause | `steps/01-stage-cohort/workdir/vendor/kazhdan/` |
| nlohmann/json | 3.6.1 | MIT | `steps/01-stage-cohort/workdir/vendor/nlohmann/` |
| nanoflann | 1.2.3 | BSD-3-Clause | `steps/01-stage-cohort/workdir/vendor/nanoflann/` |
| Boost | 1.60.0 | BSL-1.0 | `steps/01-stage-cohort/workdir/vendor/pdalboost/` |
| libE57Format | 2.0.1 | BSL-1.0 | `steps/01-stage-cohort/workdir/plugins/e57/libE57Format/` |
| CRC++ | vendored at 2.0.1 | BSD-3-Clause | `steps/01-stage-cohort/workdir/plugins/e57/libE57Format/contrib/CRCpp/` |
| laz-perf | 3.2.0 | Apache-2.0 | `environment/assets/lazperf/`; later cumulative trees |

These components are vendored inside the upstream PDAL 2.0.1 tree or the
laz-perf asset and are redistributed under their own licences. No separate
upstream NOTICE file exists for PDAL or laz-perf.

## 6. Required notices

- PDAL: "Copyright (c) 2019, Hobu, Inc. (howard@hobu.co)" for 2.0.1-2.4.0 and
  "Copyright (c) 2022, Hobu, Inc. (howard@hobu.co)" for 2.5.0-2.7.1. Each
  version's full BSD-3-Clause text is in the corresponding `LICENSES/` file.
- arbiter: "Copyright (c) 2015 Connor Manning".
- Google Test: "Copyright 2008, Google Inc. All rights reserved."
- Eigen: MPL-2.0 text; Eigen's source files carry their own copyright and MPL
  notices.
- Kazhdan/Bolitho: "Copyright (c) 2006, Michael Kazhdan and Matthew Bolitho.
  All rights reserved." The licence includes a Johns Hopkins University
  non-endorsement clause.
- nlohmann/json: "Copyright (c) 2013-2019 Niels Lohmann".
- nanoflann: "Copyright 2008-2009 Marius Muja ... David G. Lowe ... Copyright
  2011 Jose L. Blanco".
- Boost and libE57Format: BSL-1.0 text in `LICENSES/BSL-1.0.txt`; Boost's
  copyright notices are in the individual Boost headers.
- CRC++: "Copyright (c) 2016, Daniel Bahr. All rights reserved."
- laz-perf: Apache-2.0 text in `LICENSES/Apache-2.0-lazperf.txt`; the upstream
  tree has no `NOTICE` file.

## 7. Closing note

Task-authored benchmark content is licensed separately by the benchmark maintainers.
