# Third-Party Notices — fhir-server-version-chain

Benchmark task that builds a C11 FHIR server across the HL7 FHIR version chain,
from DSTU2 and STU3 through R4, R4B, R5 and the R6 ballot, graded with the
official HL7 validator, recorded HAPI FHIR exchanges and the published FHIR
packages.

## Upstream project

| | |
|---|---|
| Project | HL7 FHIR specification |
| URL | https://hl7.org/fhir/ |
| Pins | R4 `4.0.1` (`https://hl7.org/fhir/R4/`), R4B `4.3.0` (`https://hl7.org/fhir/R4B/`), R5 `5.0.0` (`https://hl7.org/fhir/R5/`), R6 `6.0.0-ballot3` (`https://hl7.org/fhir/6.0.0-ballot3/`), DSTU2 (`https://hl7.org/fhir/DSTU2/`), STU3 (`https://hl7.org/fhir/STU3/`) |
| Project | HAPI FHIR — HL7 FHIR Core Artifacts (`org.hl7.fhir.core`) |
| URL | https://github.com/hapifhir/org.hl7.fhir.core |
| Pin | release `6.10.4`, commit `1b90fb13f77bfd6e138db8e96f26a32195fb090d` (the `validator_cli.jar` release asset) |
| Project | FHIR test cases (`FHIR/fhir-test-cases`) |
| URL | https://github.com/FHIR/fhir-test-cases |
| Pin | no commit recorded in the task tree; the shipped `fml/*.map` files are byte-identical to the `r4b`/`r5`/`r6` `structure-mapping` maps in this repository (verified against commit `cb4eac4cc37626d4d79a3f206b2d5db212cc6d32`) |
| Project | Blaze (interface reference only) |
| URL | https://github.com/samply/blaze |
| Pin | none; no Blaze code or binary is bundled or executed by this task |

Bundled or derived material:

- Each step's `workdir/SPEC/{R4,R4B,R5,R6,DSTU2,STU3}/` holds HTML pages
  copied verbatim from the official HL7 publication; each directory's
  `PROVENANCE*.md` records the source URL, byte count and sha256 of every page.
- `environment/fhir-packages.txt` and each step's `tests/fhir-packages.txt`
  record the published FHIR NPM packages, versions, URLs and sha256 values.
  The packages are installed into the container's FHIR package cache; their
  contents are not copied into the task tree.
- The HAPI validator jar is downloaded and sha256-checked in each verifier's
  `Dockerfile`; the jar is not copied into the task tree.
- Test fixtures under `steps/*/tests/data/` include `fml/*.map` files copied
  byte-for-byte from the `FHIR/fhir-test-cases` `structure-mapping` suite, the
  corresponding `*.map.conv.json` conversion outputs produced with the HAPI
  6.10.4 validator, and recorded validator verdicts (`valdiff-*`,
  `valexpected.json`). The FML `manifest.json` records the suite name and the
  conversion oracle.
- The C FHIR server, instructions, harnesses and scoring logic are
  task-authored.

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| CC0-1.0 | `CC0-1.0.txt` | HL7 FHIR specification pages in `steps/*/workdir/SPEC/` and the FHIR-defined content of the published FHIR packages (official CC0 1.0 Universal legal code) |
| Apache-2.0 | `Apache-2.0.txt` | HAPI FHIR `org.hl7.fhir.core` at release `6.10.4` (repository `LICENSE.txt`) and `FHIR/fhir-test-cases` structure-mapping maps (repository `LICENSE.txt`); the two upstream license files are byte-identical Apache-2.0 text |
| BSD-3-Clause | `BSD-3-Clause-HAPI.txt` | HAPI FHIR Java sources in the validator jar, whose file headers carry the BSD-3-Clause terms with `Copyright (c) 2011+, HL7, Inc.` (exact header text from the pinned source) |

HAPI's repository `LICENSE.txt` is Apache-2.0 while its Java source files carry
BSD-3-Clause headers; both are reproduced because the validator jar contains
the compiled sources. `FHIR/fhir-test-cases` also publishes its suite under
Apache-2.0. The FHIR specification and the FHIR packages are published by HL7
under CC0-1.0 ("No Rights Reserved"). No Blaze content is bundled, so no Blaze
license file is included.

## Modifications made by the benchmark

- The specification HTML pages are copied verbatim; only the directory layout
  and the `PROVENANCE*.md` indexes are task-authored.
- The FHIR packages are installed unmodified from their published archives;
  the task records their sha256 values rather than editing them.
- HAPI's published `validator_cli.jar` is used unmodified as an oracle; the
  recorded verdicts and the FML conversion maps are derived from running that
  validator or from the HAPI test suite.
- The C FHIR server, the step ladder, the instructions, the local test
  harnesses and the scoring logic are task-authored.

## Third-party dependencies bundled in the task

| Component | Version | License | Bundled path |
|---|---|---|---|
| HL7 FHIR specification pages | R4 4.0.1, R4B 4.3.0, R5 5.0.0, R6 6.0.0-ballot3, DSTU2, STU3 | CC0-1.0 | `steps/*/workdir/SPEC/` |
| `hl7.fhir.r2.core` | 1.0.2 | CC0-1.0 | image cache `/root/.fhir/packages/hl7.fhir.r2.core#1.0.2/package` (not in the task tree) |
| `hl7.fhir.r3.core` | 3.0.2 | CC0-1.0 | image cache `/root/.fhir/packages/hl7.fhir.r3.core#3.0.2/package` |
| `hl7.fhir.r4.core` | 4.0.1 | CC0-1.0 | image cache `/root/.fhir/packages/hl7.fhir.r4.core#4.0.1/package` |
| `hl7.fhir.r4.expansions` | 4.0.1 | CC0-1.0 | image cache `/root/.fhir/packages/hl7.fhir.r4.expansions#4.0.1/package` |
| `hl7.fhir.r4b.core` | 4.3.0 | CC0-1.0 | image cache `/root/.fhir/packages/hl7.fhir.r4b.core#4.3.0/package` |
| `hl7.fhir.r5.core` | 5.0.0 | CC0-1.0 | image cache `/root/.fhir/packages/hl7.fhir.r5.core#5.0.0/package` |
| `hl7.fhir.r6.core` | 6.0.0-ballot3 | CC0-1.0 | image cache `/root/.fhir/packages/hl7.fhir.r6.core#6.0.0-ballot3/package` |
| `hl7.terminology`, `hl7.terminology.r4` | 6.5.0, 5.3.0 | CC0-1.0 (FHIR-defined content) | image cache under `/root/.fhir/packages/` |
| `hl7.fhir.uv.extensions`, `hl7.fhir.us.core`, `hl7.fhir.uv.ips`, `hl7.fhir.uv.subscriptions-backport`, `hl7.fhir.uv.bulkdata`, `hl7.fhir.uv.smart-app-launch` | 5.2.0, 6.1.0, 1.1.0, 1.1.0, 2.0.0, 2.1.0 | CC0-1.0 (FHIR-defined content) | image cache under `/root/.fhir/packages/` |
| HAPI FHIR validator | 6.10.4 | Apache-2.0; BSD-3-Clause source headers | verifier image `/opt/validator_cli.jar` (not in the task tree) |
| FHIR test cases — structure-mapping suite | unpinned in tree; maps match `FHIR/fhir-test-cases` `r4b`/`r5`/`r6` | Apache-2.0 | `steps/*/tests/data/fml/*.map` (conversion outputs in `*.map.conv.json`) |

The exact package list and sha256 values are in `environment/fhir-packages.txt`
and the per-step `tests/fhir-packages.txt`.

## Required notices

- FHIR specification, from the HL7 FHIR license page: `Copyright © 2011+ HL7.
  This specification ... is produced by HL7 under the terms of HL7® Governance
  and Operations Manual relating to Intellectual Property (Section 16) ...
  This document is licensed under Creative Commons "No Rights Reserved"
  (CC0).`
- FHIR trademarks: `HL7®, HEALTH LEVEL SEVEN®, FHIR® and the FHIR ® are
  trademarks owned by Health Level Seven International, registered with the
  United States Patent and Trademark Office.`
- HAPI FHIR: the repository `LICENSE.txt` is the Apache License 2.0; Java
  source headers carry `Copyright (c) 2011+, HL7, Inc. All rights reserved.`
  and the BSD-3-Clause conditions, reproduced in
  `LICENSES/BSD-3-Clause-HAPI.txt`.
- The HAPI FHIR repository has no Apache `NOTICE` file at the pin, so no
  additional Apache NOTICE text applies.
- FHIR test cases: the `FHIR/fhir-test-cases` `LICENSE.txt` is the unmodified
  Apache License 2.0 text and carries no project-specific copyright line. The
  conversion outputs in `*.map.conv.json` were produced with the HAPI 6.10.4
  validator from the Apache-2.0 maps.
- Blaze is named only as the interface shape the task follows; no Blaze source,
  binary or documentation is redistributed, so no Blaze notice is required.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
