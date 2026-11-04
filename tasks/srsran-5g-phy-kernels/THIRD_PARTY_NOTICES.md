# Third-party notices — srsran-5g-phy-kernels

Optimize the srsRAN Project 5G NR physical-layer DSP kernels one kernel at a
time: cumulative steps, each rewarded by the measured per-function speedup of
the implementation the library installs, gated on byte-identical output.

## Upstream project

| | |
|---|---|
| Project | srsRAN Project |
| URL | https://github.com/srsran/srsRAN_Project |
| Pin | tag `release_25_10`, commit `d2f4b70dda8e2c557d5b05a0ac5f92dbddda19bc` |
| Bundled | Full upstream source tree cloned into `/app/srsran` at image build time (`environment/Dockerfile`), minus `.git`, with the optimized implementations of the six ladder stages removed; a prebuilt CMake tree is left at `/app/build` |
| Derived | Six `steps/*/solution/oracle.patch` files containing the removed implementations, restored by the reference solution |

The `environment/vendor/PIN` file records the repository, tag and commit. The
strip stage (`environment/vendor/strip.py`) lists each stage and the files,
factory arms and CMake entries it removes.

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| AGPL-3.0-or-later | `LICENSES/AGPL-3.0-or-later.txt` | srsRAN Project (`LICENSE`; source headers offer "either version 3 of the License, or (at your option) any later version") |
| MIT | `LICENSES/MIT-fmt.txt` | `external/fmt/` — {fmt} 11.1.3 (`LICENSE`) |
| MIT | `LICENSES/MIT-nlohmann-json.txt` | `external/nlohmann/json.hpp`, `json_fwd.hpp` — JSON for Modern C++ 3.12.0 (`LICENSE.MIT`) |
| MIT | `LICENSES/MIT-backward-cpp.txt` | `external/Backward/backward.hpp` — backward-cpp (`LICENSE.txt`, Copyright 2013 Google Inc.) |
| MIT | `LICENSES/MIT-rigtorp.txt` | `external/rigtorp/MPMCQueue.h`, `SPSCQueue.h` — Erik Rigtorp's queues (`LICENSE`) |
| MIT (text in file headers) | — | `docs/doxygen-awesome.css`, `docs/doxygen-awesome-sidebar-only.css` — Doxygen Awesome, Copyright 2021-2022 jothepro |
| BSD-3-Clause | `LICENSES/BSD-3-Clause-CLI11.txt` | `external/CLI/CLI11.hpp` — CLI11 2.4.2 (`LICENSE`) |
| BSD-2-Clause | `LICENSES/BSD-2-Clause-concurrentqueue.txt` | `external/cameron314/concurrentqueue.h` — moodycamel queue, Copyright 2013-2020 Cameron Desrochers; the upstream file is also dual-licensed under the Boost Software License |
| CC0-1.0 | `LICENSES/CC0-1.0-expected.txt` | `external/TartanLlama/expected.hpp` — tl::expected 1.1.0, Copyright 2017 Sy Brand |
| Apache-2.0 | `LICENSES/Apache-2.0-uWebSockets.txt` | `external/uWebSockets/` including `uWebSockets/uSockets/` — Copyright 2018-2020 Alex Hultman |
| AGPL-3.0-or-later | `LICENSES/AGPL-3.0-or-later.txt` | `external/cmake-sbom/sbom.cmake` — the file in the upstream tree carries srsRAN's AGPL header; upstream's `COPYRIGHT` separately lists a 2023-2024 Jochem Rutgers MIT attribution for this path |

`LICENSES/AGPL-3.0-or-later.txt` is the exact upstream `LICENSE` file from the
pinned commit (SHA-256
`57c8ff33c9c0cfc3ef00e650a1cc910d7ee479a8bc509f6c9209a7c2a11399d6`), the GNU
Affero General Public License version 3 text. The "or later" option comes from
the source headers, which state "either version 3 of the License, or (at your
option) any later version".

## Version-to-stage map

| Step | Stage key in `strip.py` | Upstream kernel family |
|---|---|---|
| `01-ldpc-encode` | `ldpc_encode` | LDPC encoding |
| `02-ldpc-rate-dematch` | `ldpc_rate_dematch` | LDPC rate dematching |
| `03-ldpc-decode` | `ldpc_decode` | LDPC decoding |
| `04-precode` | `precode` | Channel precoding |
| `05-demodulate` | `demodulate` | Channel demodulation |
| `06-ofh-compression` | `ofh_bfp`, `ofh_pack` | Open Fronthaul compression |

All six steps use the same upstream pin and the same AGPL-3.0-or-later
licence; only the stripped kernel family differs.

## Modifications made by the benchmark

- `strip.py` removes, for every architecture (x86 AVX2/AVX-512 and aarch64
  NEON), the per-ISA implementation files, shared support headers whose
  earliest user is the stage, the non-portable factory arms and the matching
  CMake source entries for the six stages. The portable reference kernels are
  left untouched and are both the correctness reference and the scoring
  denominator.
- `.git` is removed after checkout. The tree is configured and built once at
  image build time with the flags in `environment/vendor/PIN`, leaving
  `/app/build`; no upstream source is otherwise edited.
- The `steps/*/solution/oracle.patch` files are benchmark-derived patches that
  restore the removed upstream implementations; they are not upstream files.
- `environment/vendor/bench_phy.cpp`, `build_harness.sh`, `strip.py` and
  `PIN` are authored benchmark content.
- The instructions, verifiers and per-step harnesses are authored benchmark
  content.

## Third-party dependencies bundled in the task

The full upstream tree is present in the image, so every vendored dependency
ships with it even where the graded libraries do not use it.

| Component | Version | License | Bundled path |
|---|---|---|---|
| {fmt} | 11.1.3 (`FMT_VERSION 110103`) | MIT | `external/fmt/` |
| JSON for Modern C++ | 3.12.0 | MIT | `external/nlohmann/json.hpp`, `json_fwd.hpp` |
| backward-cpp | vendored snapshot at the pin | MIT | `external/Backward/backward.hpp` |
| Erik Rigtorp MPMCQueue / SPSCQueue | 2018 header snapshot | MIT | `external/rigtorp/` |
| CLI11 | 2.4.2 | BSD-3-Clause | `external/CLI/CLI11.hpp` |
| moodycamel concurrentqueue | vendored snapshot (Copyright 2013-2020) | BSD-2-Clause (Boost option offered upstream) | `external/cameron314/concurrentqueue.h` |
| tl::expected | 1.1.0 | CC0-1.0 | `external/TartanLlama/expected.hpp` |
| uWebSockets and uSockets | vendored snapshot (Copyright 2018-2020) | Apache-2.0 | `external/uWebSockets/` |
| cmake-sbom | vendored snapshot | AGPL-3.0-or-later per the file header; MIT attribution in upstream `COPYRIGHT` | `external/cmake-sbom/sbom.cmake` |
| Doxygen Awesome CSS | Copyright 2021-2022 jothepro | MIT (text in each file's header) | `docs/doxygen-awesome*.css` |

Versions are those declared by the vendored headers where the header declares
one; otherwise the component is a snapshot taken at the upstream pin.

## Required notices

srsRAN:

> Copyright: 2021-2025, Software Radio Systems Limited.

Upstream's `COPYRIGHT` file (shipped in the source tree) lists the vendored
components and their copyright lines; the entries above reproduce its
attributions:

> external/Backward/backward.hpp — Copyright: 2013-2017 Google Inc.
>
> external/fmt/ — Copyright: 2012-present Victor Zverovich and {fmt}
> contributors
>
> external/CLI/CLI11.hpp — Copyright: 2017-2024 University of Cincinnati,
> developed by Henry Schreiner
>
> external/rigtorp/MPMCQueue.h, external/rigtorp/SPSCQueue.h — Copyright: 2018
> Erik Rigtorp
>
> external/cameron314/concurrentqueue.h — Copyright: 2013-2020, Cameron
> Desrochers
>
> external/TartanLlama/expected.hpp — Copyright: 2017 Sy Brand
>
> external/uWebSockets/ — Copyright: 2018-2020 Alex Hultman
>
> external/nlohmann/json.hpp — Copyright: 2013-2025 Niels Lohmann
>
> docs/doxygen-awesome*.css — Copyright: 2021-2024 jothepro

The task is not affiliated with or endorsed by the srsRAN Project or its
copyright holders.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
