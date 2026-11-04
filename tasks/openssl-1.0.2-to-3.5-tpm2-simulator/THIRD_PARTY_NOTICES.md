# Third-Party Notices — openssl-1.0.2-to-3.5-tpm2-simulator

Benchmark task that migrates the OpenSSL-backed crypto engine of a TPM 2.0
reference simulator from the vendored OpenSSL 1.0.2 archives to system OpenSSL
releases, one release at a time, without changing any answer the engine or the
simulator gives.

## Upstream projects

| | |
|---|---|
| Simulator | TPM 2.0 reference simulator (stwagnr/tpm2simulator) |
| URL | https://github.com/stwagnr/tpm2simulator |
| Pin | commit `e45324eba268723d39856111e7933c5c76238481` |
| Bundled/derived | `environment/app/` is the repository at the pin, unmodified. It carries a vendored OpenSSL 1.0.2b: headers under `environment/app/lib/openssl/` and static archives under `lib/x86-64/`, `lib/arm/`, `lib/x86/` and `lib/libeay32.lib`. Step 1 removes that vendored copy; later steps migrate the engine to the staged system OpenSSL releases. Step solutions and tests are task-authored. |
| Library | OpenSSL |
| URL | https://github.com/openssl/openssl |
| Pins | 1.0.2b (vendored); 1.1.0l; 1.1.1w; 3.0.17; 3.1.8; 3.2.5; 3.4.2; 3.5.4 (staged install trees under `environment/ossl/`) |
| Bundled/derived | The staged releases are upstream `make install` trees (headers, libraries, modules, pkg-config files), shipped as `blob-<sha256-prefix>.tar.xz` and XOR-wrapped as `.bin` files so that the ladder is not listed by `ls`. No OpenSSL source is present in the workspace. |

## OpenSSL version map

| Step | OpenSSL release installed | License |
|---|---|---|
| Starting point / vendored | 1.0.2b | OpenSSL + SSLeay |
| 01-opaque-types | 1.1.0l | OpenSSL + SSLeay |
| 02-rng-and-key-objects | 1.1.1w | OpenSSL + SSLeay |
| 03-providers-and-fetch | 3.0.17 | Apache-2.0 |
| 04-evp-symmetric | 3.0.17 | Apache-2.0 |
| 05-evp-asymmetric | 3.0.17 | Apache-2.0 |
| 06-fips-and-libctx | 3.1.8 | Apache-2.0 |
| 07-params-not-ctrls | 3.2.5 | Apache-2.0 |
| 08-no-deprecated | 3.4.2 | Apache-2.0 |
| 09-opaque-symmetric-keys | 3.5.4 | Apache-2.0 |
| 10-hardening | 3.5.4 | Apache-2.0 |

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| BSD-2-Clause | `BSD-2-Clause.txt` | TPM 2.0 reference simulator at the pin (exact in-tree `environment/app/LICENSE`) |
| OpenSSL AND SSLeay | `OpenSSL-1.0.2b.txt` | OpenSSL 1.0.2b (vendored). Exact upstream `LICENSE` from tag `OpenSSL_1_0_2b`, containing both the OpenSSL License and the original SSLeay License. `OpenSSL-vendored-Copyright.txt` is the in-tree notice carried by the vendored headers (`environment/app/lib/OpenSSL Copyright.txt`). |
| OpenSSL AND SSLeay | `OpenSSL-1.1.0l.txt` | OpenSSL 1.1.0l. Exact upstream `LICENSE` from tag `OpenSSL_1_1_0l`. |
| OpenSSL AND SSLeay | `OpenSSL-1.1.1w.txt` | OpenSSL 1.1.1w. Exact upstream `LICENSE` from tag `OpenSSL_1_1_1w`. |
| Apache-2.0 | `Apache-2.0.txt` | OpenSSL 3.0.17, 3.1.8, 3.2.5, 3.4.2 and 3.5.4. Exact upstream `LICENSE.txt` from tag `openssl-3.5.4`; the file is byte-identical across those five tags. |

The OpenSSL/SSLeay license is the same dual license across 1.0.2 and 1.1.x.
Its copyright year range and a few words of the preamble change between
releases (`Copyright (c) 1998-2011` in 1.0.2b, `1998-2018` in 1.1.0l,
`1998-2019` in 1.1.1w), so each pinned 1.0.2/1.1.x release is matched to the
exact upstream `LICENSE` for its tag.

## Modifications made by the benchmark

- `environment/app/` is the simulator repository at the pin, unmodified except
  that the task tree is committed directly rather than through a submodule.
- The vendored OpenSSL 1.0.2b headers and static archives are part of the
  upstream repository; the benchmark does not alter them. Step 1's solution
  removes them from `/app`.
- The staged OpenSSL releases are unmodified upstream install trees. The
  benchmark only re-packs them (tar + xz), names them by SHA-256 prefix, and
  XOR-wraps them with per-step keys; no OpenSSL source or binary content is
  changed.
- The step instructions, setup scripts, tests and recorded answers are
  task-authored.

## Third-party dependencies bundled in the task

| Component | Version | License | Bundled path |
|---|---|---|---|
| OpenSSL (headers only) | 1.0.2b-era | OpenSSL + SSLeay | `environment/app/lib/openssl/` |
| OpenSSL (static archives) | 1.0.2b-era | OpenSSL + SSLeay | `environment/app/lib/x86-64/libcrypto.a`, `libssl.a`; `environment/app/lib/arm/libcrypto.a`, `libssl.a`; `environment/app/lib/x86/`; `environment/app/lib/libeay32.lib` |
| OpenSSL install trees | 1.1.0l, 1.1.1w, 3.0.17, 3.1.8, 3.2.5, 3.4.2, 3.5.4 | OpenSSL + SSLeay (1.1.x); Apache-2.0 (3.x) | `environment/ossl/blob-*.tar.xz` and `blob-*.bin` |
| OpenSSL copyright notice | — | OpenSSL + SSLeay | `environment/app/lib/OpenSSL Copyright.txt` |

No other third-party library is vendored in this task. The base image supplies
the compiler, CMake, Clang/LLVM and Python tooling under their own licenses.

## Required notices

- Simulator: `Copyright (c) 2015, Steffen Wagner`, as carried in
  `LICENSES/BSD-2-Clause.txt`.
- OpenSSL 1.0.2 / 1.1.x: `Copyright (c) 1998-2011` in 1.0.2b, `1998-2018` in
  1.1.0l and `1998-2019` in 1.1.1w, all `The OpenSSL Project`, plus the SSLeay
  attribution to Eric A. Young and Tim J. Hudson. Each is carried in the
  matching `LICENSES/OpenSSL-1.*.txt` file. The vendored headers' in-tree
  notice says `Copyright (c) 1998-2003 The OpenSSL Project`.
- OpenSSL 3.x: `Copyright (c) 1998-2025 The OpenSSL Project` (3.0.17,
  3.1.8) or `Copyright (c) 1998-2025 The OpenSSL Project Authors` (3.2.5,
  3.4.2, 3.5.4), together with `Copyright (c) 1995-1998 Eric A. Young, Tim J.
  Hudson`, as stated in each release's `README.md`. The Apache-2.0
  `LICENSE.txt` itself carries no copyright line.
- No `NOTICE` file accompanies the OpenSSL 3.x license text.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
