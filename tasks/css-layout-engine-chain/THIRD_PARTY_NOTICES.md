# Third-Party Notices

Task: `css-layout-engine-chain` — a five-step CSS layout engine in C, carrying
one W3C WebDriver remote end from CSS 2.1 through `calc()`, custom properties and
flex layout to CSS Grid Level 1.

## Upstream projects and pins

| item | value |
| --- | --- |
| CSS specifications | W3C Technical Reports, https://www.w3.org/TR/ |
| HTML specification | WHATWG HTML Review Draft 2026-07, https://html.spec.whatwg.org/review-drafts/2026-07/ |
| Test corpus | Web Platform Tests (WPT)-derived fixtures plus benchmark-authored fixtures |
| Documentation pins | `environment/docs-provenance.txt` records the exact URL and SHA-256 of every document; `environment/docs/SHA256SUMS` records the extracted files |
| What is bundled/derived | The ten W3C documents below and the WHATWG review draft are copied into `environment/docs/`. `environment/corpus/documents/` holds 279 files: 68 WPT-derived HTML fixtures, 210 benchmark-generated HTML fixtures, and `Ahem.ttf`. Each step ships an earlier slice of the same corpus in `workdir/css-corpus/` and `tests/fixtures/`. |

### Document pin map

| document | dated edition | license link in the document |
| --- | --- | --- |
| CSS 2.1 | `REC-CSS2-20110607` (7 June 2011) | W3C document use rules |
| WebDriver 1 | `REC-webdriver1-20180605` (5 June 2018) | W3C Software and Document License 2015 |
| CSSOM 1 | `WD-cssom-1-20210826` (26 August 2021) | W3C Software and Document License 2015 |
| CSS Variables 1 | `CR-css-variables-1-20220616` (16 June 2022) | W3C Software and Document License 2015 |
| CSS Values 3 | `CRD-css-values-3-20240322` (22 March 2024) | W3C Software and Document License 2023 |
| CSS Grid 1 | `CRD-css-grid-1-20250326` (26 March 2025) | W3C Software and Document License 2023 |
| CSS Flexbox 1 | `CRD-css-flexbox-1-20251014` (14 October 2025) | W3C Software and Document License 2023 |
| CSS Sizing 3 | `WD-css-sizing-3-20260904` (4 September 2026) | W3C Software and Document License 2023 |
| CSS Syntax 3 | `CRD-css-syntax-3-20261001` (1 October 2026) | W3C Software and Document License 2023 |
| CSS Align 3 | `WD-css-align-3-20261008` (8 October 2026) | W3C Software and Document License 2023 |
| WHATWG HTML | Review Draft 2026-07 | CC BY 4.0; code portions BSD 3-Clause |

## License table

| SPDX ID | File under `LICENSES/` | Covers |
| --- | --- | --- |
| W3C Document License (2002) | `W3C-Document-License-2002.txt` | CSS 2.1 Recommendation (`REC-CSS2-20110607`), whose document use rules are the 2002 document license |
| W3C Software and Document License (2015) | `W3C-Software-and-Document-License-2015.txt` | WebDriver 1 (2018), CSSOM 1 (2021), CSS Variables 1 (2022) |
| W3C Software and Document License (2023) | `W3C-Software-and-Document-License-2023.txt` | CSS Values 3 (2024), CSS Grid 1 (2025), CSS Flexbox 1 (2025), CSS Sizing 3 (2026), CSS Syntax 3 (2026), CSS Align 3 (2026) |
| CC-BY-4.0 | `CC-BY-4.0.txt` | WHATWG HTML Review Draft 2026-07 |
| BSD-3-Clause | `BSD-3-Clause.txt` | WPT-derived fixtures. Exact WPT `LICENSE.md`, copyright © web-platform-tests contributors. The WHATWG license also offers BSD-3-Clause for portions incorporated into source code. |
| CC0-1.0 | `CC0-1.0.txt` | Ahem font fallback declaration |
| Public domain / CC0 | `Ahem-Public-Domain.txt` | Ahem font, exact public-domain/CC0 statement from the font's name table |

The W3C license texts were extracted from the publisher's license pages:
`https://www.w3.org/copyright/document-license-2002/`,
`https://www.w3.org/copyright/software-license-2015/` and
`https://www.w3.org/copyright/software-license-2023/`.

## Modifications made by the benchmark

- Documents were downloaded from the dated W3C `/TR/` snapshots and the WHATWG
  review-draft URL, checked against the pinned SHA-256 sums, and copied in
  without textual edits. The CSS 2.1 tarball was extracted; its `css2.txt` is
  also stored separately. No document license notice was removed or altered.
- The 68 WPT-derived fixtures keep their original test markup, author/reviewer
  metadata and assertions. The benchmark renames them with a
  `wpt-<suite>__` prefix and inserts
  `<style>@font-face { font-family: 'Ahem'; src: url('Ahem.ttf') format('truetype'); }</style>`
  immediately before `</head>` so the fixture loads the bundled Ahem font. Some
  reference links in the original tests point at WPT reference files that are
  not shipped; the recorded expectations are in `environment/corpus/expected.json`.
- The 210 benchmark-generated fixtures were authored for this task from the
  cited CSS specifications. Their expected rectangles and resolved values were
  recorded from ChromeDriver through the same WebDriver client used to grade the
  candidate.
- `Ahem.ttf` is copied unmodified (version 1.50); every copy in the step
  fixtures and workdir corpora is byte-identical to
  `environment/corpus/documents/Ahem.ttf`.
- No step's tests, instructions or `task.toml` are changed by this notice.

## Third-party dependencies bundled in the task

| name | version | license | bundled path |
| --- | --- | --- | --- |
| Ahem font | 1.50 | public domain; CC0 declaration where public domain is not recognized | `environment/corpus/documents/Ahem.ttf`, `steps/*/tests/fixtures/Ahem.ttf`, `steps/*/workdir/css-corpus/documents/Ahem.ttf` |
| Web Platform Tests fixtures | files derived from WPT; no single WPT release is pinned | BSD-3-Clause | `environment/corpus/documents/wpt-*` and the per-step corpus slices |
| W3C CSS/WebDriver documents | dated editions in the table above | W3C document/software licenses above | `environment/docs/w3c/` |
| WHATWG HTML Review Draft | 2026-07 | CC-BY-4.0; BSD-3-Clause for code portions | `environment/docs/whatwg/html-review-draft-2026-07.html` |

No CSS, HTML or layout library is bundled or installed in the task image.

## Required notices

- CSS 2.1: `Copyright © 2011 W3C® (MIT, ERCIM, Keio), All Rights Reserved.
  W3C liability, trademark and document use rules apply.`
- WebDriver 1: `Copyright © 2018 W3C® (MIT, ERCIM, Keio, Beihang)`.
- CSSOM 1: `Copyright © 2021 W3C® (MIT, ERCIM, Keio, Beihang)`.
- CSS Variables 1: `Copyright © 2022 W3C® (MIT, ERCIM, Keio, Beihang)`.
- CSS Values 3: `Copyright © 2024 World Wide Web Consortium`.
- CSS Grid 1: `Copyright © 2025 World Wide Web Consortium`.
- CSS Flexbox 1: `Copyright © 2025 World Wide Web Consortium`.
- CSS Sizing 3: `Copyright © 2026 World Wide Web Consortium`.
- CSS Syntax 3: `Copyright © 2026 World Wide Web Consortium`.
- CSS Align 3: `Copyright © 2026 World Wide Web Consortium`.
- WHATWG HTML: `Copyright © WHATWG (Apple, Google, Mozilla, Microsoft). This
  work is licensed under a Creative Commons Attribution 4.0 International
  License. To the extent portions of it are incorporated into source code, such
  portions in the source code are licensed under the BSD 3-Clause License
  instead.`
- WPT: `Copyright © web-platform-tests contributors`. The BSD-3-Clause text is
  in `LICENSES/BSD-3-Clause.txt`.
- Ahem: `The Ahem font belongs to the public domain. In jurisdictions that do
  not recognize public domain ownership of these files, the following Creative
  Commons Zero declaration applies:
  http://labs.creativecommons.org/licenses/zero-waive/1.0/us/legalcode`
  The font was developed by Todd Fahrner and Myles C. Maxfield.
- W3C®, CSS, HTML and WHATWG are trademarks or names of their respective
  owners. This task claims no endorsement by W3C, WHATWG or the WPT project.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
