# Third-Party Notices — mldsa-native-avx2-asm

**Task.** Six-step performance-optimization benchmark that optimizes the
mldsa-native ML-DSA post-quantum signature library one arithmetic backend
stage at a time, rewarded by per-entry-point measured speedup gated on
bit-exact output against the portable C reference that stays in the tree.

## 1. Upstream project

| | |
| --- | --- |
| Project | mldsa-native |
| URL | <https://github.com/pq-code-package/mldsa-native> |
| Pin | tag `v2.0.0`, commit `834a90d5e846ffa1e1611bd24e160bb2e9b86d35` |
| Licence declaration | `Apache-2.0 OR ISC OR MIT` for the library, per the upstream `LICENSE`; every file carries its own SPDX-License-Identifier, and files outside the library may carry different terms |
| Upstream provenance | fork of the public-domain Dilithium reference implementation, <https://github.com/pq-crystals/dilithium> |

The upstream source is not committed to this task directory. The image build
(`environment/Dockerfile`) and every verifier (`steps/*/tests/vendor/prepare_base.sh`)
clone the repository at the pinned tag, assert the commit id, and strip the
ladder's implementations as described in §4. The upstream `LICENSE` and
`RELICENSE.md` travel with each clone; `RELICENSE.md` records the contributors'
consent to the `Apache-2.0 OR ISC OR MIT` relicensing and adds no separate
terms.

## 2. License files

| SPDX ID | File under `LICENSES/` | Upstream component / versions covered |
| --- | --- | --- |
| `Apache-2.0 OR ISC OR MIT` | `mldsa-native-LICENSE.txt` | Exact upstream `LICENSE` at `v2.0.0` (16,152 bytes, byte-identical to the file at the pin); its composite grant plus the complete Apache-2.0, ISC and MIT texts cover the mldsa-native library, tests and other files carrying those identifiers |
| Apache-2.0 | `Apache-2.0.txt` | Full Apache License 2.0 text (11,361 bytes), with the upstream copyright line `Copyright (c) The mldsa-native project authors` filled into the appendix; the first alternative of the library licence |
| ISC | `ISC.txt` | Exact ISC grant from the upstream `LICENSE` (745 bytes), `Copyright (c) The mldsa-native project authors`; the second alternative |
| MIT | `MIT.txt` | Exact MIT grant from the upstream `LICENSE` (1,079 bytes), `Copyright (c) The mldsa-native project authors`; the third alternative |
| CC0-1.0 | `CC0-1.0.txt` | Exact Creative Commons CC0 1.0 Universal legal code (7,048 bytes); alternative for `test/notrandombytes` |
| 0BSD | `0BSD.txt` | SPDX 0BSD template text (643 bytes); alternative for `test/notrandombytes` |
| MIT-0 | `MIT-0.txt` | SPDX MIT No Attribution template text (915 bytes); alternative for `test/notrandombytes` |
| `LicenseRef-PD-hp` | `LicenseRef-PD-hp.txt` | The one-line public-domain dedication `This work is hereby placed into the public domain.` defined by D. J. Bernstein at <https://cr.yp.to/spdx.html>; first alternative for `test/notrandombytes` |
| MIT | `MIT-tiny_sha3.txt` | Exact `LICENSE` bundled in the upstream tree at `examples/custom_backend/mldsa_native/src/fips202/native/custom/src/LICENSE` (1,138 bytes), `Copyright (c) 2015 Markku-Juhani O. Saarinen`; covers the `tiny_sha3` example code |
| CC-BY-4.0 | `CC-BY-4.0.txt` | Exact Creative Commons Attribution 4.0 International legal code (18,657 bytes); covers the upstream Markdown documentation |

## 3. What is bundled or derived

- The image's `/app/mldsa-native` is the complete upstream tree at the pin,
  minus `.git`, with the optimized implementations of the seven ladder stages
  removed (§4). That includes upstream's `test/hal/`, `test/notrandombytes/`,
  `examples/*/test_only_rng/`, `scripts/notrandombytes`, the two `tiny_sha3`
  example copies and the Markdown documentation, all unmodified. The upstream
  `LICENSE` remains at `/app/mldsa-native/LICENSE`.
- Each verifier image additionally bakes three copies of the pinned source:
  `/upstream`, an unstripped complete upstream tree used only for a non-scored
  similarity diagnostic; `/l0`, a fully stripped floor tree; and `/base`, the
  step's reference tree as handed to the candidate. `/upstream` therefore
  retains `dev/`, `proofs/`, `integration/`, `scripts/autogen/`,
  `scripts/simpasm/`, `mldsa/src/native/aarch64/` and `.github/`, together with
  the upstream `LICENSE`.
- `steps/01-transform-ntt-intt/solution/oracle.patch` through
  `steps/06-polyz-unpack/solution/oracle.patch` — six task-derived patches that
  restore one stage each. They contain upstream source for:
  - step 01: `mldsa_ntt_avx2_asm.S`, `mldsa_intt_avx2_asm.S`,
    `mldsa_nttunpack_avx2_asm.S`, `consts.c`, `consts.h` and
    `arith_native_x86_64.h`;
  - step 02: `mldsa_pointwise_avx2_asm.S`, the three
    `mldsa_pointwise_acc_l{4,5,7}_avx2_asm.S`, `mldsa_poly_caddq_avx2_asm.S`
    and `mldsa_poly_chknorm_avx2_asm.S`;
  - step 03: `mldsa_poly_decompose_32_avx2_asm.S` and
    `mldsa_poly_use_hint_32_avx2_asm.S`;
  - step 04: `mldsa_poly_decompose_88_avx2_asm.S` and
    `mldsa_poly_use_hint_88_avx2_asm.S`;
  - step 05: `mldsa_rej_uniform_avx2_asm.S`,
    `mldsa_rej_uniform_eta2_avx2_asm.S`,
    `mldsa_rej_uniform_eta4_avx2_asm.S` and `rej_uniform_table.c`;
  - step 06: `mldsa_polyz_unpack_17_avx2_asm.S` and
    `mldsa_polyz_unpack_19_avx2_asm.S`.
  Each patch also carries the corresponding upstream edits to the amalgamated
  `mldsa/mldsa_native.c` and `mldsa/mldsa_native_asm.S`, the backend hooks in
  `mldsa/src/native/x86_64/meta.h`, and the ABI checker's
  `test/abicheck/x86_64/checks_x86_64_all.h`. The upstream copyright and SPDX
  headers are retained in the patched content.
- `environment/vendor/PIN` and `environment/vendor/strip.py`, mirrored in each
  `steps/*/tests/vendor/` tree — task-authored vendor tooling, not upstream
  content.
- Task-authored: the six `instruction.md` files, `tests/`, `solution/solve.sh`,
  `solution/SHA256SUMS`, each step's `BENCHMARK.md` and
  `WORKSPACE_HANDOFF.md`, `environment/Dockerfile` and `environment/timer.sh`.

## 4. Modifications made by the benchmark

`environment/vendor/strip.py` removes the optimized implementation of each
ladder stage from the copied upstream tree:

- stages `ntt` and `intt` (step 01), `pointwise` (step 02), `decompose32`
  (step 03), `decompose88` (step 04), `rej` (step 05) and `unpack` (step 06),
  deleting each stage's `*_avx2_asm.S` files and the shared AVX2 constant block
  (`consts.c`, `consts.h`, `arith_native_x86_64.h`) when the first stage that
  needs them goes;
- the matching `MLD_USE_NATIVE_*` toggles and `mld_*_native` bodies from
  `mldsa/src/native/x86_64/meta.h`;
- every reference to the deleted files from the single-compilation-unit
  amalgamations, the ABI-checker registry and per-kernel lists, and the
  benchmark source.

It also deletes these upstream directories and files, none of which is
reachable from the library build but each of which would disclose an optimized
implementation or embed its compiled bytecode: `dev/`, `proofs/`,
`integration/`, `scripts/autogen/`, `scripts/simpasm/`,
`mldsa/src/native/aarch64/` and `.github/`.

The deletions are applied to the candidate tree `/app`, the reference tree
`/base` and the floor tree `/l0`; the verifier's `/upstream` diagnostic copy is
left unstripped and retains all of them.

The portable C implementations are retained as the correctness oracle and the
speedup denominator. The stripped tree is otherwise the upstream tree:
`test/hal/`, `test/notrandombytes/`, the examples, the documentation and the
build system are unmodified apart from the reference removals above. The
oracle patches are the benchmark's derived restoration of upstream code, not
upstream modifications; their content is the upstream implementation at the
pin.

## 5. Third-party dependencies bundled in the task

| Component | Version | License | Bundled path |
| --- | --- | --- | --- |
| mldsa-native | `v2.0.0` (commit `834a90d5e846ffa1e1611bd24e160bb2e9b86d35`) | `Apache-2.0 OR ISC OR MIT` | image `/app/mldsa-native`; exact `LICENSE` reproduced as `LICENSES/mldsa-native-LICENSE.txt`; upstream sources also embedded in the six `solution/oracle.patch` files |
| `tiny_sha3` (Markku-Juhani O. Saarinen) | as vendored in mldsa-native `v2.0.0`; no separate version string | MIT | image `examples/bring_your_own_fips202/custom_fips202/tiny_sha3/` and `examples/custom_backend/mldsa_native/src/fips202/native/custom/src/tiny_sha3/`; upstream `LICENSE` reproduced as `LICENSES/MIT-tiny_sha3.txt` |
| `notrandombytes` | as vendored in mldsa-native `v2.0.0`; no separate version string | `LicenseRef-PD-hp OR CC0-1.0 OR 0BSD OR MIT-0 OR MIT` | image `test/notrandombytes/`, its symlinked copies in `examples/*/test_only_rng/` and `scripts/notrandombytes`; used for testing only |
| `test/hal` benchmarking code | as vendored in mldsa-native `v2.0.0`; no separate version string | MIT | image `test/hal/hal.c` and `test/hal/hal.h`; used for testing only |
| verification proofs in `proofs/` (partly derived from Amazon Web Services verification infrastructure) | as vendored in mldsa-native `v2.0.0`; no separate version string | `Apache-2.0 OR ISC OR MIT-0`, `MIT-0`, or `MIT-0 AND Apache-2.0`; `proofs/isabelle/neon_ntt/document/iacrtrans.cls` is CC0-1.0 | verifier image `/upstream/proofs/` (the unstripped diagnostic copy); not part of the library or of `/app` |

No other third-party source library is stored in this task directory. The
image is built from `debian:bookworm-slim` with ordinary C/C++ build and test
packages (gcc, g++, make, git, patch, python3, gawk and companion utilities);
those are image-level components under their own distribution licenses, not
files stored in the task directory.

## 6. Required notices

- mldsa-native: the upstream `LICENSE` carries the copyright block
  `Copyright (c) The mldsa-native project authors`,
  `Copyright (c) The mlkem-native project authors`,
  `Copyright (c) 2020 Dougall Johnson` and
  `Copyright (c) 2022 Arm Limited` in its MIT notice for the test and
  benchmarking code. The ISC and library-MIT sections carry
  `Copyright (c) The mldsa-native project authors`.
- Apache-2.0: the upstream repository at `v2.0.0` contains no `NOTICE` file, so
  no Apache NOTICE text is reproduced. The Apache-2.0 text's standard
  trademark clause is the only trademark language in the upstream `LICENSE`.
- `tiny_sha3`: `Copyright (c) 2015 Markku-Juhani O. Saarinen`, MIT. The
  upstream license file carries a `CC-BY-4.0` Markdown header of its own while
  reproducing the MIT text; it is reproduced verbatim.
- `notrandombytes`: `Copyright (c) The mldsa-native project authors`, derived
  from <https://cr.yp.to/papers.html#surf> and offered under
  `LicenseRef-PD-hp OR CC0-1.0 OR 0BSD OR MIT-0 OR MIT`. `LicenseRef-PD-hp` is
  the one-line public-domain dedication defined at
  <https://cr.yp.to/spdx.html>; it is not an SPDX-listed identifier and has no
  separate license file upstream.
- CC BY 4.0 attribution for the Markdown documentation in the upstream tree:
  "mldsa-native documentation" by The mldsa-native project authors, from
  <https://github.com/pq-code-package/mldsa-native> at `v2.0.0`, licensed under
  Creative Commons Attribution 4.0 International. The documentation is
  redistributed as part of the cloned tree without modification.
- `proofs/` is removed from the candidate `/app` tree, but the verifier's
  unstripped `/upstream` copy retains it. Its files carry
  `Apache-2.0 OR ISC OR MIT-0`, `MIT-0`, or `MIT-0 AND Apache-2.0`, and
  `proofs/isabelle/neon_ntt/document/iacrtrans.cls` carries `CC0-1.0`. The
  corresponding texts are in `LICENSES/Apache-2.0.txt`, `LICENSES/ISC.txt`,
  `LICENSES/MIT-0.txt` and `LICENSES/CC0-1.0.txt`; the CC0-1.0 and MIT-0 files
  are also alternatives for `notrandombytes`.
- Copyright lines carried by the files in the pinned tree and the embedded
  patches: `Copyright (c) The mldsa-native project authors`,
  `Copyright (c) The mlkem-native project authors`,
  `Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.`,
  `Copyright (c) 2020 Dougall Johnson`,
  `Copyright (c) 2021-2022 Arm Limited`,
  `Copyright (c) 2022 Arm Limited`,
  `Copyright (c) 2022 Hanno Becker`,
  `Copyright (c) 2022 Matthias Kannwischer`,
  `Copyright (c) 2023 Amin Abdulrahman, Matthias Kannwischer`,
  `Copyright (c) Arm Ltd.`,
  `Copyright (c) 2025 Arm Limited` and
  `Copyright (c) 2026 Arm Limited`.
- No separate trademark or endorsement statement accompanies the upstream work
  beyond the Apache-2.0 standard trademark clause.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
