# Third-Party Notices — svtjpegxs-codec-avx2

An eight-step benchmark ladder in which a candidate optimizes SVT-JPEG-XS's
encode and decode pipeline stages one stage at a time, with each step gated on
bit-exact output and rewarded by measured per-function speedup.

## Upstream project

| | |
|---|---|
| Project | SVT-JPEG-XS (Scalable Video Technology for JPEG XS) |
| Repository | https://github.com/OpenVisualCloud/SVT-JPEG-XS |
| Pin | commit `f9c82c12beed453980ef1888a5e72923b7265ed4` |
| Upstream license | BSD-2-Clause-Patent (`LICENSE.md`; upstream describes it as "BSD+Patent") |

The task repository does not vendor the SVT-JPEG-XS source tree. The agent image
and every verifier image clone the pinned commit at build time and remove the
runtime-dispatched implementation bodies with `prepare.py` and `strip.py`.

### Bundled and derived material

* `steps/*/solution/oracle.patch` — eight patches derived from upstream
  `Source/Lib/{Encoder,Decoder}/ASM_*` sources. They restore the optimized
  implementations and dispatch wiring for the eight ladder stages
  (`nlt_enc`, `dwt_fwd`, `quant`, `gcli`, `rate_ctrl`, `unpack`, `idwt`,
  `nlt_dec`). The added code retains the upstream Intel copyright and
  BSD-2-Clause-Patent headers.
* `environment/vendor/bench/*.c`, `bench.h` and `build_bench.sh` —
  benchmark-authored per-function measurement harness.
* `environment/vendor/prepare.py`, `strip.py`, `PIN` — benchmark-authored
  tree preparation and stripping tooling.
* `environment/Dockerfile`, `timer.sh`, `task.toml` and the step
  `instruction.md` / `tests/` content — task-authored benchmark material.
* The built image also keeps the upstream sample picture
  `Build/linux/PGO/1080p_422_8b.yuv` at `/app/samples/1080p_422_8b.yuv`.
* No upstream source tree is committed to the task repository.

## Licenses

| SPDX ID | File under `LICENSES/` | Applies to |
|---|---|---|
| BSD-2-Clause-Patent | `BSD-2-Clause-Patent.txt` | SVT-JPEG-XS at the pin. Verbatim upstream `LICENSE.md`, Copyright (c) 2024 Intel Corporation. Also covers the upstream sample picture. |
| BSD-2-Clause | `BSD-2-Clause-cpuinfo.txt` | `third_party/cpuinfo/LICENSE` in the upstream tree (cpuinfo, Copyright (c) 2019 Google LLC and others). |
| BSD-2-Clause | `BSD-2-Clause-clog.txt` | `third_party/cpuinfo/deps/clog/LICENSE` (clog, Copyright (C) 2018 Marat Dukhan and others). |
| BSD-3-Clause | `BSD-3-Clause-googletest.txt` | `third_party/googletest-1.14.0/LICENSE` (GoogleTest 1.14.0, Copyright 2008 Google Inc.). |

## Modifications made by the benchmark

* `prepare.py` copies the pinned checkout without `.git`, removes the AVX-512
  directories and every build reference to them, and deletes unit-test sources
  that cannot link without the removed symbols. It also adds guarded CMake
  files so an emptied `ASM_*` directory still builds.
* `strip.py` removes, for each ladder stage, the per-ISA bodies of the dispatch
  slots that stage owns and rewires those slots to the portable C reference.
  The portable C paths and the encoder/decoder pipeline remain intact.
* The AVX-512 tier is out of scope for the whole ladder and is removed from the
  starting tree.
* Verifier images rebuild the identically stripped base and measure against it;
  the oracle patches restore the upstream optimized implementations.
* No behavioural change is made to the remaining upstream code: the stripped
  tree still encodes and decodes byte-identically.

## Third-party dependencies bundled in the task

The task repository contains no third-party code beyond the derived
SVT-JPEG-XS patches. The upstream tree cloned at image-build time carries the
following vendored dependencies, unmodified by the benchmark:

| Name | Version | License | Bundled path |
|---|---|---|---|
| cpuinfo | vendored at the pin | BSD-2-Clause | `third_party/cpuinfo/` in the image tree; used for CPU feature detection |
| clog | vendored at the pin (inside cpuinfo) | BSD-2-Clause | `third_party/cpuinfo/deps/clog/` |
| GoogleTest | 1.14.0 | BSD-3-Clause | `third_party/googletest-1.14.0/`; built because the harness configures with `BUILD_TESTING=ON` |

The system toolchain and CMake/Ninja packages installed by the Dockerfile are
build dependencies from Debian and are not redistributed in the task tree.

## Required notices

The upstream `LICENSE.md` carries:

```
Copyright (c) 2024 Intel Corporation
```

The restored source files carry the upstream header:

```
/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/
```

Third-party copyright lines, reproduced in the corresponding `LICENSES/` files:

- cpuinfo: `Copyright (c) 2019 Google LLC`, `Copyright (c) 2017-2018 Facebook
  Inc.`, `Copyright (C) 2012-2017 Georgia Institute of Technology`,
  `Copyright (C) 2010-2012 Marat Dukhan`.
- clog: `Copyright (C) 2018 Marat Dukhan`, `Copyright (c) 2017-2018 Facebook
  Inc.`, `Copyright (c) 2017 Georgia Institute of Technology`.
- GoogleTest: `Copyright 2008, Google Inc.`

The upstream project ships no separate `NOTICE` file at this pin, and
`LICENSE.md` contains no trademark statement.

---

Task-authored benchmark content is licensed separately by the benchmark maintainers.
