# Third-Party Notices

Task: `boringssl-crypto-asm` — optimize BoringSSL's AES and public-key primitives
one kernel group at a time across five cumulative steps, rewarded by measured
per-function speedup gated on bit-exact output.

## Upstream project and pin

| item | value |
| --- | --- |
| Project | BoringSSL |
| URL | https://github.com/google/boringssl |
| Tag | `0.20260903.0` |
| Commit | `0ce57bbf06a35864fa39e3b9346d41e833683383` |
| Pin file | `environment/vendor/PIN` |
| What is bundled/derived | The image clones the full upstream tree at this commit, removes `.git`, applies the benchmark stripper, and pre-builds the `crypto` target. The task tree itself stores the pin, the Dockerfile and `environment/vendor/strip.py`; the per-step oracle patches derive from the same upstream source. |

## License table

| SPDX ID | File under `LICENSES/` | Covers |
| --- | --- | --- |
| Apache-2.0 | `Apache-2.0.txt` | BoringSSL's original `libcrypto`/`libssl` source and the Apache-2.0 text reproduced at the head of upstream's `LICENSE` |
| BSD-3-Clause | `BSD-3-Clause.txt` | The Go-authored part of the TLS test suite, copyright (c) 2009 The Go Authors, reproduced at the tail of upstream's `LICENSE` |
| Apache-2.0 + BSD-3-Clause | `BoringSSL-LICENSE.txt` | Verbatim upstream `LICENSE` at the pin, containing both licenses and the sentence that the Go-licensed support code is not compiled into `libcrypto`/`libssl` |

Upstream `LICENSE` is the authoritative combined file. It contains no separate
BoringSSL copyright line; the only copyright line in it is
`Copyright (c) 2009 The Go Authors. All rights reserved.` for the support code.
`AUTHORS` at the pin lists Google LLC, Brian Smith, Apple Inc and contributors
to OpenSSL-derived code, but is not itself a license grant.

## Modifications made by the benchmark

`environment/vendor/strip.py` removes, for each of the five stage groups
`aes_block`, `aes_gcm`, `gcm_siv`, `bn_mont` and `p256`:

- the authored perlasm (`.pl`) for every architecture (x86, x86_64, arm,
  aarch64), not only x86_64;
- the pre-generated assembly under `gen/`;
- the stage's entries in the generated file lists and `build.json`;
- the dispatch region that installs the optimized implementation.

The portable implementations (`*_nohw.cc.inc`, `p256.cc.inc`,
`montgomery.cc.inc`, `generic.cc.inc`, and the C path in
`crypto/cipher/e_aesgcmsiv.cc`) are preserved and remain the correctness
reference. `.git` is removed. The tree is then configured with CMake/Ninja and
`crypto` is built once. No upstream license file is modified.

## Third-party dependencies bundled in the task

No third-party dependency is vendored in the task tree. BoringSSL's own upstream
tree contains code derived from OpenSSL; that code is distributed under
BoringSSL's `LICENSE`, which covers it. The Go-licensed TLS test support code is
part of the upstream tree but is not linked into `libcrypto` or `libssl`.

## Required notices

- BoringSSL is a fork of OpenSSL; the upstream `LICENSE` covers the combined
  work. The `AUTHORS` file records the OpenSSL contributors.
- Go support code: `Copyright (c) 2009 The Go Authors. All rights reserved.`
  The BSD-3-Clause text is in `LICENSES/BSD-3-Clause.txt`.
- No `NOTICE` file exists at the pinned commit, so no Apache NOTICE text is
  reproduced.
- BoringSSL and Google are trademarks of their respective owners. This task
  does not claim endorsement by or affiliation with Google.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
