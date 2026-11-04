# Third-Party Notices — isal-crypto-multibuffer-hash

**Task.** Seven-step performance-optimization benchmark that restores Intel
isa-l_crypto's multi-buffer hashing implementations one hash at a time, gated
on byte-exact digests and measured per-API speedup.

## 1. Upstream project

| | |
| --- | --- |
| Project | Intel isa-l_crypto |
| URL | <https://github.com/intel/isa-l_crypto> |
| Pin | tag `v2.26.1`, commit `c353c2d021c03cfc6180ddf9e28a24b3b61e9760` |
| Bundled/derived | The image clones the full upstream tree at the pin and removes the optimized implementations of the ladder's nine hash stages. The `steps/*/solution/oracle.patch` files are derived from the upstream optimized kernels and restore one stage per step. |

## 2. License files

| SPDX ID | File under `LICENSES/` | Upstream component / versions covered |
| --- | --- | --- |
| BSD-3-Clause | `BSD-3-Clause.txt` | isa-l_crypto `v2.26.1` root `LICENSE` (exact upstream text, `Copyright(c) 2011-2024 Intel Corporation All rights reserved.`) |
| BSD-3-Clause | `BSD-3-Clause-Intel-ipsec-mb.txt` | `intel-ipsec-mb/LICENSE` as bundled inside isa-l_crypto `v2.26.1` (exact upstream text, `Copyright (c) 2012-2023, Intel Corporation`) |

## 3. What is bundled or derived

- The image's `/app/isal_crypto` tree is the upstream source at the pin, minus
  `.git`, with the stage implementations listed below removed. The portable C
  reference implementations remain.
- `steps/01-sha256-mb` through `steps/07-mh-sha256-rolling-hash`
  `solution/oracle.patch` — task-derived patches that restore each step's
  upstream optimized implementation.
- `intel-ipsec-mb/` — a subset of the Intel Multi-Buffer Crypto for IPsec
  library bundled inside the upstream isa-l_crypto tree: `LICENSE`,
  `README.md` and eleven `lib/include/*.inc` files. It is carried unmodified.
- Task-authored: `instruction.md`, `tests/`, `environment/vendor/strip.py`,
  `environment/vendor/PIN`, `tools/practice.c`, `tools/measure.sh` and
  `timer.sh`.

## 4. Modifications made by the benchmark

`environment/vendor/strip.py` removes, from the copied upstream tree, the
optimized multi-buffer implementation of each ladder stage:

`sha256_mb`, `sha1_mb`, `sha512_mb`, `md5_mb`, `sm3_mb`, `mh_sha1`,
`mh_sha1_murmur3_x64_128`, `mh_sha256`, `rolling_hash`.

For each stage directory it:

- deletes every `*.asm` file (the kernels, the lane-scheduling manager and the
  `*_multibinary.asm` ISA dispatcher);
- deletes `aarch64/` and `riscv64/`, which carry the same multi-buffer
  widening for other architectures;
- removes the deleted sources from `Makefile.am` and `cmake/<stage>.cmake`,
  re-registering the stage's `*_base_aliases.c` sources so the public API
  resolves to the portable C implementation on x86-64;
- removes the corresponding per-kernel probes from `test/abicheck/` and
  references to the removed kernels from the amalgamated sources, headers,
  build files and benchmark files.

The C reference implementations (`*_base.c`, `*_ref.c`) are retained: they are
the correctness oracle and the speedup denominator. `Makefile.nmake` is
regenerated with the upstream `make -f Makefile.unx Makefile.nmake` rule.
No other upstream file content is edited.

## 5. Third-party dependencies bundled in the task

| Component | Version | License | Bundled path |
| --- | --- | --- | --- |
| Intel Multi-Buffer Crypto for IPsec (`intel-ipsec-mb`) | as vendored inside isa-l_crypto `v2.26.1` (no separate version string; README only) | BSD-3-Clause | `intel-ipsec-mb/` in the image tree: `LICENSE`, `README.md`, `lib/include/*.inc` (11 files) |

`libssl-dev` is installed in the verifier image because upstream's performance
and random tests link OpenSSL; it is not stored in the task directory.

## 6. Required notices

- isa-l_crypto `v2.26.1`: `Copyright(c) 2011-2024 Intel Corporation All rights
  reserved.` The root `LICENSE` ends with
  `SPDX-License-Identifier: BSD-3-Clause`.
- `intel-ipsec-mb`: `Copyright (c) 2012-2023, Intel Corporation`.
- Neither isa-l_crypto nor its `intel-ipsec-mb` subtree ships a `NOTICE` file
  at this pin. No trademark statement is made in either license file.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
