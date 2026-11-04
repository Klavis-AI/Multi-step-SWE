# Third-Party Notices — ooxml-spreadsheet-engine-multiversion

Benchmark task that implements `seedcalc`, an OOXML SpreadsheetML package
reader, writer and formula engine, across ten cumulative qax-os/excelize
release milestones, from the OPC container and writer through pivot caches,
streaming output, a formula evaluator and ECMA-376 decryption.

## Upstream projects

| | |
|---|---|
| Project | Excelize |
| URL | https://github.com/qax-os/excelize |
| Pins | `v1.1.0`, `v1.4.1`, `v2.0.1`, `v2.0.2`, `v2.1.0`, `v2.2.0`, `v2.3.2`, `v2.4.0`, `v2.5.0`, `v2.6.0` |
| Bundled/derived | Step 01's solution archive is the upstream `v1.1.0` source tree with the Go package/module renamed to `github.com/harbor-bench/seedcalc`; steps 02–10 apply release-to-release patches derived from the corresponding upstream releases, carrying the same rename. The seeded workspace itself contains no Excelize source. Each solution archive or patch retains the upstream `LICENSE` for its release. |
| Specifications | ECMA-376 Parts 1–3, OASIS OpenDocument v1.3 OS Part 4 (OpenFormula), and [MS-OFFCRYPTO], shipped verbatim under `steps/01-opc-package-and-writer/workdir/specs/`. |

## Version map

| Step | Source checkpoint | Target checkpoint |
|---|---|---|
| 01-opc-package-and-writer | — | `v1.1.0` |
| 02-worksheet-features-and-iteration | `v1.1.0` | `v1.4.1` |
| 03-workbook-metadata-and-errors | `v1.4.1` | `v2.0.1` |
| 04-pivot-caches-and-macro-part | `v2.0.1` | `v2.0.2` |
| 05-streaming-writer-and-drawings | `v2.0.2` | `v2.1.0` |
| 06-formula-tokenizer-and-evaluator | `v2.1.0` | `v2.2.0` |
| 07-decryption-limits-and-criteria | `v2.2.0` | `v2.3.2` |
| 08-function-library-and-expressions | `v2.3.2` | `v2.4.0` |
| 09-criteria-lookup-and-budget | `v2.4.0` | `v2.5.0` |
| 10-number-format-engine | `v2.5.0` | `v2.6.0` |

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| BSD-3-Clause | `BSD-3-Clause.txt` | Excelize `v1.1.0`. Exact copy of the workspace's `LICENSE-UPSTREAM-REFERENCE`, identical to the upstream `LICENSE` at tag `v1.1.0`; `Copyright (c) 2016 - 2017 Ri Xu`. |
| BSD-3-Clause | `BSD-3-Clause-excelize-v1.4.1.txt` | Excelize `v1.4.1`; `Copyright (c) 2016 - 2019 360 Enterprise Security Group, Endpoint Security, inc.` |
| BSD-3-Clause | `BSD-3-Clause-excelize-v2.0.1-v2.0.2.txt` | Excelize `v2.0.1` and `v2.0.2`; 360 Enterprise Security Group and Geoffrey J. Teale. |
| BSD-3-Clause | `BSD-3-Clause-excelize-v2.1.0.txt` | Excelize `v2.1.0`; 360 Enterprise Security Group and Geoffrey J. Teale. |
| BSD-3-Clause | `BSD-3-Clause-excelize-v2.2.0-v2.3.2.txt` | Excelize `v2.2.0` and `v2.3.2`; `Copyright (c) 2016-2020 The excelize Authors.` |
| BSD-3-Clause | `BSD-3-Clause-excelize-v2.4.0-v2.5.0.txt` | Excelize `v2.4.0` and `v2.5.0`; `Copyright (c) 2016-2021 The excelize Authors.` |
| BSD-3-Clause | `BSD-3-Clause-excelize-v2.6.0.txt` | Excelize `v2.6.0`; `Copyright (c) 2016-2022 The excelize Authors.` |
| BSD-3-Clause | `BSD-3-Clause-Go.txt` | `golang.org/x/crypto`, `golang.org/x/net`, `golang.org/x/text`; `Copyright 2009 The Go Authors.` Exact in-tree `vendor/golang.org/x/net/LICENSE`. |
| BSD-3-Clause | `BSD-3-Clause-efp.txt` | `github.com/xuri/efp v0.0.1`; `Copyright (c) 2017 - 2025 Ri Xu`. Exact in-tree `vendor/github.com/xuri/efp/LICENSE`. |
| BSD-3-Clause | `BSD-3-Clause-nfp.txt` | `github.com/xuri/nfp v0.0.2-0.20250530014748-2ddeb826f9a9`; `Copyright (c) 2022-2025 Ri Xu`. Exact in-tree `vendor/github.com/xuri/nfp/LICENSE`. |
| MIT | `MIT.txt` | `github.com/tiendc/go-deepcopy v1.7.2`; `Copyright (c) 2023 tiendc`. Exact in-tree `vendor/github.com/tiendc/go-deepcopy/LICENSE`. |
| MIT | `MIT-mohae.txt` | `github.com/mohae/deepcopy v0.0.0-20170929034955-c48cc78d4826`; `Copyright (c) 2014 Joel`. Exact in-tree `vendor/github.com/mohae/deepcopy/LICENSE`. |
| Apache-2.0 | `Apache-2.0.txt` | `github.com/richardlehane/mscfb v1.0.7` and `github.com/richardlehane/msoleps v1.0.6`. Exact in-tree `vendor/github.com/richardlehane/mscfb/LICENSE.txt`; the Apache text carries no project copyright line. |
| — | `Go-PATENTS.txt` | Additional patent grant accompanying the three `golang.org/x/` modules. Exact in-tree `vendor/golang.org/x/net/PATENTS`. |
| — | `ECMA-376-Notice.txt` | ECMA-376 Parts 1, 2 and 3. Ecma copyright licence and disclaimer, as extracted on p. viii of the Part 3 PDF. |
| — | `OASIS-OpenDocument-Notice.txt` | OASIS OpenDocument v1.3 OS Part 4 (OpenFormula). Full "Notices" block, `Copyright © OASIS Open 2021`. |
| — | `MS-OFFCRYPTO-Notice.txt` | [MS-OFFCRYPTO]. Microsoft Open Specifications intellectual-property-rights notice. |

Excelize has used BSD-3-Clause at every pinned release, but its copyright
holder and year range changed six times; the table maps each release to the
license file that reproduces that release's exact `LICENSE`.

## Specification corpus

The step-1 workspace ships these documents verbatim; sizes and SHA-256 digests
are recorded in `steps/01-opc-package-and-writer/workdir/specs/MANIFEST.md`.

| Document | Edition / revision | Source |
|---|---|---|
| ECMA-376 Part 1 — Fundamentals and Markup Language Reference | 5th edition, December 2016 | `https://ecma-international.org/wp-content/uploads/ECMA-376-1_5th_edition_december_2016.zip` |
| ECMA-376 Part 2 — Open Packaging Conventions | 5th edition, December 2021 | `https://ecma-international.org/wp-content/uploads/ECMA-376-2_5th_edition_december_2021.zip` |
| ECMA-376 Part 3 — Markup Compatibility and Extensibility | 5th edition, December 2015 | `https://ecma-international.org/wp-content/uploads/ECMA-376-3_5th_edition_december_2015.zip` |
| OASIS OpenDocument v1.3 OS, Part 4: Formula | OASIS Standard, 2021 | `https://docs.oasis-open.org/office/OpenDocument/v1.3/os/part4-formula/OpenDocument-v1.3-os-part4-formula.html` |
| [MS-OFFCRYPTO] — Office Document Cryptography Structure | v20260217, revision 14.0, 2026-02-17 | `https://officeprotocoldocs-f5hpbjgea6b8gneq.b02.azurefd.net/files/MS-OFFCRYPTO/%5bMS-OFFCRYPTO%5d.pdf` |

The ECMA notice is the verbatim copyright page from the Part 3 PDF (the Part 1
and Part 2 PDFs have no extractable copyright page and are governed by the same
Ecma boilerplate). The Microsoft file reproduces the complete "Intellectual
Property Rights Notice for Open Specifications Documentation" from Microsoft's
Open Specifications landing page for [MS-OFFCRYPTO]; the same notice is printed
on page 1 of the shipped PDF (a shorter excerpt is recorded in
`specs/MANIFEST.md`).

## Modifications made by the benchmark

- The upstream Go sources are renamed from the Excelize package to the
  benchmark's `github.com/harbor-bench/seedcalc` module, including import paths;
  this is the only edit applied to upstream source.
- Step 01's solution archive is the `v1.1.0` source tree under that rename.
  Steps 02–10 apply benchmark-recorded release-to-release patches derived from
  the upstream tags in the version map; the patches update the upstream
  `LICENSE` when it changed between releases.
- The seeded workspace is a scaffold: it carries the pinned `go.mod`/`go.sum`,
  the `vendor/` dependency set, the `specs/` corpus, and task-authored
  `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md` and `timer.sh`. Step instructions,
  tests and verifier logic are task-authored.
- The specification PDFs, ZIP schema archives and HTML document are shipped
  byte-for-byte as downloaded; no document was converted, edited or re-rendered.

## Third-party dependencies bundled in the task

All are vendored under `steps/01-opc-package-and-writer/workdir/vendor/` and
listed in `vendor/modules.txt`.

| Component | Version | License | Bundled path |
|---|---|---|---|
| github.com/mohae/deepcopy | `v0.0.0-20170929034955-c48cc78d4826` | MIT | `vendor/github.com/mohae/deepcopy/` |
| github.com/richardlehane/mscfb | `v1.0.7` | Apache-2.0 | `vendor/github.com/richardlehane/mscfb/` |
| github.com/richardlehane/msoleps | `v1.0.6` | Apache-2.0 | `vendor/github.com/richardlehane/msoleps/` |
| github.com/tiendc/go-deepcopy | `v1.7.2` | MIT | `vendor/github.com/tiendc/go-deepcopy/` |
| github.com/xuri/efp | `v0.0.1` | BSD-3-Clause | `vendor/github.com/xuri/efp/` |
| github.com/xuri/nfp | `v0.0.2-0.20250530014748-2ddeb826f9a9` | BSD-3-Clause | `vendor/github.com/xuri/nfp/` |
| golang.org/x/crypto | `v0.53.0` | BSD-3-Clause + Go PATENTS | `vendor/golang.org/x/crypto/` |
| golang.org/x/net | `v0.56.0` | BSD-3-Clause + Go PATENTS | `vendor/golang.org/x/net/` |
| golang.org/x/text | `v0.38.0` | BSD-3-Clause + Go PATENTS | `vendor/golang.org/x/text/` |

The Go toolchain itself is supplied by the base image and is not vendored.

## Required notices

- Excelize: the copyright line for each pinned release is reproduced in the
  corresponding `BSD-3-Clause-excelize-*.txt` file (see the license table).
- Go standard-library-derived modules: `Copyright 2009 The Go Authors.`, plus
  the patent grant in `Go-PATENTS.txt`.
- `github.com/xuri/efp` and `github.com/xuri/nfp`: `Copyright (c) 2017 - 2025
  Ri Xu` and `Copyright (c) 2022-2025 Ri Xu` respectively.
- `github.com/mohae/deepcopy`: `Copyright (c) 2014 Joel`.
  `github.com/tiendc/go-deepcopy`: `Copyright (c) 2023 tiendc`.
- Apache-2.0 components (`mscfb`, `msoleps`) carry no project copyright line in
  their LICENSE files. Neither module ships a `NOTICE` file, so no additional
  Apache NOTICE text applies.
- ECMA-376: `(c) 2015 Ecma International`; the documents may be redistributed
  verbatim but not modified.
- OASIS OpenDocument v1.3 Part 4: `Copyright © OASIS Open 2021. All Rights
  Reserved.` The document may be copied and distributed unmodified with the
  notice intact; the name "OASIS" is a trademark of OASIS.
- [MS-OFFCRYPTO]: Microsoft copyright; the grant is limited to copying the
  documentation to develop implementations and to distributing schemas, IDLs
  or code samples in those implementations. All other rights reserved.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
