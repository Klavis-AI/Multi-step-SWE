# Third-Party Notices: quic-http3-stack-multiversion

## 1. Task

Implement a QUIC transport and HTTP/3 stack across fifteen public release
milestones of cloudflare/quiche, graded against the QUIC and HTTP/3 RFCs and
the candidate's own client and server binaries over a real UDP process
boundary.

## 2. Upstream projects and pins

| Upstream | URL | Pins | Bundled or derived in this task |
|---|---|---|---|
| quiche | https://github.com/cloudflare/quiche | 0.1.0, 0.3.0, 0.5.0, 0.7.0, 0.9.0, 0.11.0, 0.14.0, 0.16.0, 0.18.0, 0.20.0, 0.22.0, 0.23.0, 0.24.3, 0.26.0, 0.30.0 | Step 01's `solution/quiche-0.1.0.tar.zst` is the upstream 0.1.0 tree with `.git`, `target`, `.github`, `fuzz/corpus` and the BoringSSL submodule excluded. Steps 02-15 carry `git diff <previous>..<target>` patches over the upstream repository, verified at authoring time against the real target trees. Each step's solution `Cargo.lock` is benchmark-authored; upstream does not ship lock files. |
| BoringSSL | https://github.com/google/boringssl | commits `f18bd55240b229a65df48e7905da98fff18cbf59`, `1e859054c31ec7b974e998373e85fadea56d93ad`, `88024df12147e56b6abd66b743ff441a0aaa09a8`, `597b810379e126ae05d32c1d94b1a9464385acd0`, `067cfd92f4d7da0edfa073b096d090b98a83b860`, `f1c75347daa2ea81a941e953f2263e0a4d970c8d` | Built into the environment image at `/opt/bssl/<sha>` and linked through `QUICHE_BSSL_PATH`; the BoringSSL source is not stored in the task tree. Step 01 uses `f18bd552`, step 02 `1e859054`, step 03 `88024df1`, step 04 `597b8103`, step 05 `067cfd92`, and steps 06-14 `f1c75347`. |
| boring / boring-sys | https://crates.io/crates/boring, https://crates.io/crates/boring-sys | 5.2.0 each | Step 15 drops the BoringSSL submodule and links the crates.io `boring` binding; `boring-sys` compiles its own BoringSSL copy. The crate sources are fetched into the image registry cache, not stored in the task tree. |
| IETF RFCs | https://www.rfc-editor.org/ | RFC 9000, 9001, 9002, 9114, 9204, 9218, 9221, 9297, 9368, 9369; RFC 9001 Appendix A extract | Each step's `workdir/spec/` carries the RFC texts byte-identically, plus `rfc9001-appendix-a.txt` and `appendix-a-vectors.json` derived from RFC 9001 Appendix A. `SHA256SUMS` covers the ten full texts. |

The candidate's implementation, tests and harness are authored for the
benchmark.

## 3. License map

| SPDX ID | File under `LICENSES/` | Component / version covered |
|---|---|---|
| BSD-2-Clause | `BSD-2-Clause-0.1.0.txt` | quiche 0.1.0; exact upstream `COPYING`, Copyright (C) 2018 Cloudflare, Inc. and Copyright (C) 2018 Alessandro Ghedini |
| BSD-2-Clause | `BSD-2-Clause.txt` | quiche 0.3.0-0.30.0; exact upstream `COPYING`, Copyright (C) 2018-2019 Cloudflare, Inc. The file is byte-identical at every later tag; step 02's patch updates the 0.1.0 file to this text. |
| — (composite BoringSSL licence) | `BoringSSL-LICENSE.txt` | BoringSSL at the six 2019-2024 pins; exact upstream `LICENSE`, containing the OpenSSL and SSLeay licences, the ISC licence for Google-authored files, the MIT licence for `third_party/fiat`, the Go licence for TLS test-suite support code, and the Chromium BSD-3-Clause licence for test data. |
| Apache-2.0 | `Apache-2.0.txt` | The `boring` crate 5.2.0 and the BoringSSL copy inside `boring-sys` 5.2.0. Canonical SPDX Apache-2.0 text. `boring-LICENSE.txt` is the exact short Apache-2.0 notice shipped by the crate. |
| — (Apache-2.0 notice) | `boring-LICENSE.txt` | `boring` 5.2.0; exact upstream `LICENSE`, Copyright 2011-2017 Google Inc., 2013 Jack Lloyd, 2013-2014 Steven Fackler, 2020 Ivan Nikulin. |
| MIT | `MIT-boring-sys.txt` | `boring-sys` 5.2.0; exact upstream `LICENSE-MIT`, Copyright (c) 2014 Alex Crichton and Copyright (c) 2020 Ivan Nikulin. |
| — (composite BoringSSL licence) | `BoringSSL-LICENSE-boring-sys.txt` | BoringSSL source vendored by `boring-sys` 5.2.0; exact `deps/boringssl/LICENSE`, Apache-2.0 plus the Go licence for TLS test-suite support code. |
| — (RFC notices) | `IETF-Trust-RFC-Notices.md` | The ten RFCs and the RFC 9001 Appendix A extract; exact copies of the `Copyright Notice` sections from the bundled texts, arranged by copyright year and BSD variant. This file is copied verbatim from `steps/*/workdir/spec/LICENCE-NOTE.md`. |

No other third-party source or data is bundled in the task tree.

## 4. Benchmark modifications

- Step 01's archive is the upstream 0.1.0 tree with `.git`, `target`,
  `.github`, `fuzz/corpus` and the BoringSSL submodule removed. All remaining
  files are byte-identical to the upstream tag.
- Steps 02-15 apply upstream `git diff` patches. The solve scripts record that
  each patch was verified against a clean checkout of the target tag.
- Each step's `Cargo.lock` is authoring material, not an upstream file.
- BoringSSL is compiled in `environment/bssl-build.sh` with `-Wno-error` for
  warnings produced by newer compilers; its source is not altered.
- The RFC texts in `workdir/spec/` are unmodified. `appendix-a-vectors.json`,
  `SHA256SUMS` and the step tests are benchmark-authored.
- The candidate implementation and verifier harness are benchmark-authored.

## 5. Bundled third-party dependencies

| Name | Version | License | Bundled path |
|---|---|---|---|
| quiche source | 0.1.0 | BSD-2-Clause | `steps/01-draft23-transport-and-h3/solution/quiche-0.1.0.tar.zst` |
| quiche source changes | 0.3.0-0.30.0 | BSD-2-Clause | `steps/02-*` through `steps/15-*`: `solution/step-*.patch.zst` |
| BoringSSL | six commits listed in section 2 | Composite: OpenSSL, SSLeay, ISC, MIT, Go, Chromium BSD-3-Clause | Built into the environment image at `/opt/bssl/<sha>`; not stored in the task tree |
| boring | 5.2.0 | Apache-2.0 | Fetched from crates.io into the image registry cache; not stored in the task tree |
| boring-sys | 5.2.0 | MIT; its vendored BoringSSL is Apache-2.0 plus Go licence | Fetched from crates.io into the image registry cache; not stored in the task tree |
| RFC 9000, 9001, 9002, 9114, 9204, 9218, 9221, 9297, 9368, 9369 | 2021-2023 | IETF Trust notices; BCP 78 | `steps/*/workdir/spec/rfc*.txt` |

The Rust toolchain image and the remaining crates.io dependency graph are
supplied by the image's package manager and registry cache. They are not
redistributed in this task tree.

## 6. Required notices

- quiche 0.1.0: "Copyright (C) 2018, Cloudflare, Inc." and "Copyright (C) 2018,
  Alessandro Ghedini. All rights reserved."
- quiche 0.3.0-0.30.0: "Copyright (C) 2018-2019, Cloudflare, Inc. All rights
  reserved."
- BoringSSL 2019-2024 pins: the composite licence text in
  `LICENSES/BoringSSL-LICENSE.txt` carries the OpenSSL copyright
  (1998-2011 The OpenSSL Project), the SSLeay copyright (1995-1998 Eric Young),
  the Google ISC notice (2015, Google Inc.), the fiat-crypto authors' MIT
  notice, the Go copyright (2009 The Go Authors) and the Chromium copyright
  (2015 The Chromium Authors).
- BoringSSL inside `boring-sys` 5.2.0: Apache-2.0 with the Go licence for TLS
  test-suite support code, in `LICENSES/BoringSSL-LICENSE-boring-sys.txt`.
- `boring` 5.2.0: "Copyright 2011-2017 Google Inc.; 2013 Jack Lloyd;
  2013-2014 Steven Fackler; 2020 Ivan Nikulin" under Apache-2.0.
- `boring-sys` 5.2.0: "Copyright (c) 2014 Alex Crichton" and "Copyright (c)
  2020 Ivan Nikulin" under the MIT licence.
- RFC 9000, 9001 and 9002: "Copyright (c) 2021 IETF Trust and the persons
  identified as the document authors. All rights reserved." Code components
  are offered under the Simplified BSD License.
- RFC 9114, 9204, 9218, 9221 and 9297: "Copyright (c) 2022 IETF Trust and the
  persons identified as the document authors. All rights reserved." Code
  components are offered under the Revised BSD License.
- RFC 9368 and 9369: "Copyright (c) 2023 IETF Trust and the persons identified
  as the document authors. All rights reserved." Code components are offered
  under the Revised BSD License.
- Each RFC notice refers to BCP 78 and the IETF Trust's Legal Provisions
  Relating to IETF Documents at https://trustee.ietf.org/license-info.

## 7. Closing note

Task-authored benchmark content is licensed separately by the benchmark maintainers.
