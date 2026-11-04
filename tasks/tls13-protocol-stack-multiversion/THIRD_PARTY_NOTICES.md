# Third-Party Notices

**Task:** `harbor/tls13-protocol-stack-multiversion` — implement a TLS 1.2 +
TLS 1.3 protocol stack in Rust across thirteen cumulative upstream rustls
releases, from v/0.19.0 through v/0.23.45.

## 1. Upstream project

| | |
| --- | --- |
| Project | rustls |
| Repository | <https://github.com/rustls/rustls> |
| Upstream licence | Apache-2.0 OR ISC OR MIT (upstream `LICENSE`) |
| Copyright | Copyright (c) 2016 Joseph Birr-Pixton `<jpixton@gmail.com>` |

Pinned releases, in step order:

| Step | Upstream tag | Commit |
| --- | --- | --- |
| 01-tls12-and-tls13-stack | `v/0.19.0` | `47d309233f2d21ccf058d35c58d684e5e4c9c643` |
| 02-connection-enum-and-strict-eof | `v/0.20.0` | `bc754a4fbb586beb7b1dfce38ab880fd90c0e422` |
| 03-deframer-hardening | `v/0.20.7` | `a863fc554aca02533a422dc228bcc938d20a721f` |
| 04-ip-peers-and-ticket-store | `v/0.21.0` | `45197b807cf0699c842fcb85eb8eca555c74cc04` |
| 05-validation-tightening | `v/0.21.6` | `40e4b5d6428edcac2275b547edb70e028beb9f31` |
| 06-crypto-provider-split | `v/0.22.0` | `4d1b762b5328a1714862ba73ec72d5522fe0c049` |
| 07-provider-ecosystem-settle | `v/0.22.2` | `5cd41a3d4f1f0648f2e1e8e8356c19731b3360c4` |
| 08-default-provider-and-hello-randomisation | `v/0.23.0` | `eb0791bc94cfdc4c42f743902a35074ce58ced11` |
| 09-close-notify-and-sni-ip | `v/0.23.5` | `14cb5d2eac709f6c9bd46c697f090bb1f1543db1` |
| 10-deframer-rewrite-and-early-data | `v/0.23.13` | `a8d875fb2096e2eee57735189868db7d7e2ba0c0` |
| 11-post-quantum-key-share | `v/0.23.22` | `784b87330788becaad1d932d0c592a4051e9ea0b` |
| 12-nonblocking-complete-io | `v/0.23.31` | `647ece13463bbb6eaf756146596fe182644424b1` |
| 13-encryption-level-alignment | `v/0.23.45` | `2976d90fd1c2db6b518700dd101b714069cfcb17` |

## 2. What is bundled or derived

- `steps/01-tls12-and-tls13-stack/solution/release-0.19.0.tar.gz` — the complete
  upstream v/0.19.0 source tree, byte-identical to `git archive v/0.19.0` with
  `test-ca/`, `bogo/bogo/` and `target/` removed.
- `steps/02-...` through `steps/13-.../solution/release-*-to-*.patch` — the
  exact upstream release-to-release source diffs. Applying all of them to the
  step 01 tree reproduces each pinned tag.
- `steps/*/solution/LICENSE`, `LICENSE-APACHE`, `LICENSE-MIT`, `LICENSE-ISC` —
  verbatim upstream licence files at every pinned release.
- `steps/*/workdir/docs/specs/` — verbatim IETF RFCs and one Internet-Draft,
  used as the protocol specification corpus.
- Benchmark-authored content: `instruction.md`, `tests/`, `spec/`,
  `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md`, `timer.sh`, and the task
  manifests. These are not derived from rustls.

## 3. Licence files

| SPDX ID | File under `LICENSES/` | Upstream component / versions covered |
| --- | --- | --- |
| Apache-2.0 | `Apache-2.0.txt` | rustls `v/0.19.0` through `v/0.23.45` |
| MIT | `MIT.txt` | rustls `v/0.19.0` through `v/0.23.45` |
| ISC | `ISC.txt` | rustls `v/0.19.0` through `v/0.23.45` |

The three files were copied byte-for-byte from
`steps/01-tls12-and-tls13-stack/solution/`; the same bytes are present in every
step and match the upstream files published at both `v/0.19.0` and `v/0.23.45`.
The upstream `LICENSE` summary reads:

> Rustls is distributed under the following three licenses:
>
> - Apache License version 2.0.
> - MIT license.
> - ISC license.
>
> These are included as LICENSE-APACHE, LICENSE-MIT and LICENSE-ISC
> respectively. You may use this software under the terms of any of these
> licenses, at your option.

## 4. Modifications made by this benchmark

- Upstream test material (`test-ca/`) and the BoringSSL BoGo corpus
  (`bogo/bogo/`) are excluded from the shipped payload; the verifier supplies
  its own copies.
- Build output (`target/`) is excluded from the task artifact.
- Frozen `Cargo.lock` files are added where upstream did not commit one, so the
  verifier can build with `cargo build --locked`.
- No upstream source file is modified. Steps 02-13 apply exact upstream
  release diffs; the benchmark's own files are listed in section 2.
- The RFC/draft corpus under `docs/specs/` is reproduced verbatim and
  unmodified.

## 5. Third-party dependencies bundled in the task

| Component | Version | Licence | Bundled path |
| --- | --- | --- | --- |
| IETF RFC corpus (RFC 5246, 5280, 5077, 6066, 6962, 7250, 7301, 7627, 8446, 8447, 8449, 9147) | as published by the RFC Editor | IETF Trust Legal Provisions (BCP 78 / BCP 79) | `steps/01-tls12-and-tls13-stack/workdir/docs/specs/rfc*.txt` |
| `draft-ietf-tls-ecdhe-mlkem` | revision 05 | IETF Trust Legal Provisions (BCP 78 / BCP 79) | `steps/01-tls12-and-tls13-stack/workdir/docs/specs/draft-ietf-tls-ecdhe-mlkem.txt` |

Rust crate dependencies are resolved from crates.io when the verifier builds
the candidate and are not vendored in this task. Their licences are not
redistributed here.

The IETF notice shipped with the corpus,
`steps/01-tls12-and-tls13-stack/workdir/docs/specs/LICENSE-NOTICE.md`, states:

> IETF documents are Copyright (c) the IETF Trust and the persons identified as
> their authors. All rights reserved. They are made available subject to the
> IETF Trust's Legal Provisions Relating to IETF Documents (BCP 78 / RFC 5378,
> and BCP 79), which permit verbatim reproduction and distribution of the
> complete document. Each document carries its own copyright notice and full
> legal text in its own body.

## 6. Required notices

- Copyright (c) 2016 Joseph Birr-Pixton `<jpixton@gmail.com>` — the copyright
  line carried by the upstream `LICENSE-MIT` and `LICENSE-ISC`.
- rustls publishes no `NOTICE` file at `v/0.19.0` or `v/0.23.45`, so there is no
  Apache NOTICE text to reproduce.
- IETF documents remain Copyright (c) the IETF Trust and their authors, and
  are reproduced under the IETF Trust Legal Provisions as described above.
- No trademark licence is granted by the upstream or by this task; rustls and
  the names of its contributors remain the property of their holders.

## 7. Scope of this licence bundle

This bundle covers the upstream rustls material and the IETF documents
redistributed with this task. Task-authored benchmark content is licensed
separately by the benchmark maintainers.
