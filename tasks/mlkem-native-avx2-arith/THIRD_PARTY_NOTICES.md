# Third-Party Notices — mlkem-native-avx2-arith

Benchmark task that asks a candidate to optimize the AVX2 arithmetic backend of
mlkem-native one primitive group at a time, gated on byte-identical ML-KEM output.

## Upstream project

| | |
|---|---|
| Project | mlkem-native |
| URL | https://github.com/pq-code-package/mlkem-native |
| Pin | tag `v2.0.0`, commit `d1b2fe782888bdb761a50336012923180be7f502` (2025-08-25) |
| Bundled/derived | The environment image clones the full upstream tree at the pin and removes the optimized arithmetic backends and authoring material listed below. The step solution patches (`steps/*/solution/oracle.patch`) are derived from the upstream AVX2 kernels and carry `SPDX-License-Identifier: Apache-2.0 OR ISC OR MIT`. The workspace handoff and benchmark documents are task-authored. |

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| Apache-2.0 | `Apache-2.0.txt` (SPDX canonical) and `mlkem-native-LICENSE.txt` (exact upstream license file, section "Apache-2.0 license") | mlkem-native v2.0.0 source under `mlkem/*` and `dev/*` |
| ISC | `ISC.txt` (SPDX canonical, with upstream copyright line) and `mlkem-native-LICENSE.txt` (section "ISC license") | mlkem-native v2.0.0 source under `mlkem/*` and `dev/*` |
| MIT | `MIT.txt` (SPDX canonical, with upstream copyright line) and `mlkem-native-LICENSE.txt` (MIT section) | mlkem-native v2.0.0; `test/hal/` and the `test/notrandombytes/` test RNG (redistributed under the MIT option) |
| CC0-1.0 | `CC0-1.0.txt` | Alternative terms offered by the `test/notrandombytes/` and valgrind-patch headers (`LicenseRef-PD-hp OR CC0-1.0 OR 0BSD OR MIT-0 OR MIT`) |
| 0BSD | `0BSD.txt` | Alternative terms offered by the same headers |
| MIT-0 | `MIT-0.txt` | Alternative terms offered by the same headers |
| CC-BY-4.0 | `CC-BY-4.0.txt` | mlkem-native v2.0.0 documentation |

`mlkem-native-LICENSE.txt` is the complete upstream `LICENSE` file at the pin; it
states that all source in `mlkem/*` and `dev/*` is offered under the choice of
Apache-2.0 OR ISC OR MIT, that files outside the library may carry different
terms indicated by their own SPDX headers, and that documentation is licensed
under CC-BY-4.0. The three license texts are reproduced in full in that file.

## Modifications made by the benchmark

The environment image starts from the upstream tree at the pin and removes, so
that no optimized arithmetic kernel is present in the workspace:

- `mlkem/src/native/aarch64`, `mlkem/src/native/ppc64le`, `mlkem/src/native/riscv64`
- `dev/` (the generator from which the x86_64 assembly is derived)
- `proofs/` (including `proofs/hol_light/x86_64/mlkem/`, which embeds the kernels)
- `scripts/autogen`, `scripts/simpasm`
- `examples/`, `integration/`
- `mlkem/mlkem_native.c`, `mlkem/mlkem_native_asm.S`

`test/abicheck/`, the build system, the functional and known-answer suites, the
portable C backend, and the FIPS-202 (Keccak) native backend remain upstream.
No upstream file content is otherwise edited by the benchmark. The task's
`BENCHMARK.md`, `WORKSPACE_HANDOFF.md`, instructions, tests and verifier are
task-authored.

## Third-party dependencies bundled in the task

The task carries the upstream mlkem-native tree at the pin. Some upstream
test-support files outside `mlkem/*` and `dev/*` remain in the workspace and
carry their own terms:

| Component | Version | License | Bundled path |
|---|---|---|---|
| `notrandombytes` test RNG (derived from Daniel J. Bernstein's SURF code) | mlkem-native v2.0.0 | `LicenseRef-PD-hp OR CC0-1.0 OR 0BSD OR MIT-0 OR MIT`; redistributed here under the MIT option | `test/notrandombytes/`, `scripts/notrandombytes` |
| HAL benchmarking helpers | mlkem-native v2.0.0 | MIT | `test/hal/` |
| valgrind varlat patch | 2024-08-08 | `LicenseRef-PD-hp OR CC0-1.0 OR 0BSD OR MIT-0 OR MIT`; redistributed under the MIT option | `nix/valgrind/valgrind-varlat-patch-20240808.txt` |

No other third-party library is vendored. Upstream notes that it is a fork of
the public domain Kyber reference implementation
(https://github.com/pq-crystals/kyber). The `tiny_sha3` code by Markku-Juhani
O. Saarinen (MIT) and the AWS-derived proof files appear only in `examples/`
and `proofs/`, which this task's environment removes.

## Required notices

- Upstream copyright holders are identified at the top of each source file, as
  stated in the upstream `LICENSE`. The license file also carries
  `Copyright (c) The mlkem-native project authors`,
  `Copyright (c) 2020 Dougall Johnson` and `Copyright (c) 2022 Arm Limited`.
- Documentation attribution: "mlkem-native documentation is licensed under
  CC-BY-4.0" per the upstream `LICENSE`. No trademark statement is made by
  upstream in that file.
- Test-support files: `Copyright (c) The mlkem-native project authors`; the
  `test/hal/` files additionally carry `Copyright (c) 2020 Dougall Johnson`
  and `Copyright (c) 2022 Arm Limited`. Their SPDX headers identify the MIT or
  `LicenseRef-PD-hp OR CC0-1.0 OR 0BSD OR MIT-0 OR MIT` terms reproduced above.
- The solution patches reproduce SPDX headers only; they do not add a new
  copyright line.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
