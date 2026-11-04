# Third-Party Notices: opentype-engine-version-chain

## 1. Task

Six-step JavaScript OpenType font engine implementing FreeType's `ftlint` and
HarfBuzz's `hb-shape` / `hb-vector` / `hb-subset` command-line interfaces, from
TrueType/CFF rasterization through OpenType Layout, complex-script shaping,
variable fonts, colour fonts and subsetting.

## 2. Upstream projects and pins

| Upstream | URL | Pin | Bundled or derived in this task |
|---|---|---|---|
| FreeType | https://gitlab.freedesktop.org/freetype/freetype | tag `VER-2-14-3` | Two headers (`fterrdef.h`, `fterrors.h`) under `environment/docs/freetype/`. FreeType itself is built only in the verifier image from the pinned tag; no FreeType source is bundled. |
| freetype-demos | https://gitlab.freedesktop.org/freetype/freetype-demos | tag `VER-2-14-3` | The `ftlint` reference tool is built in the verifier image; no source is bundled. |
| HarfBuzz | https://github.com/harfbuzz/harfbuzz | `14.4.0` | The utilities manual page `environment/docs/harfbuzz/utilities.html` (from harfbuzz.github.io commit `d7d0dfef1b438d87b7bdace9a6c99603ba6b8fee`); HarfBuzz test fonts in the corpus. HarfBuzz itself is built only in the verifier image; no HarfBuzz source is bundled. |
| OTS | https://github.com/khaledhosny/ots | tag `v9.3.0` | OTS test fonts in the corpus. OTS itself is built only in the verifier image; no OTS source is bundled. |
| text-rendering-tests | https://github.com/unicode-org/text-rendering-tests | commit not recorded in this task | A small number of test fonts in the corpus. |
| Unicode Standard | https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-3/ | Unicode 17.0.0, chapter 3 | `environment/docs/unicode/core-spec-17.0.0-chapter-3.html`. |
| W3C | https://www.w3.org/TR/2024/CRD-compositing-1-20240321/ | Compositing and Blending Level 1, CRD 21 March 2024 | `environment/docs/w3c/compositing-1-20240321.html`. |
| Microsoft OpenType | https://learn.microsoft.com/en-us/typography/opentype/spec/ | OpenType 1.9.1 pages, fetched 2026-10-19 with per-file SHA-256 in `environment/docs-manifest.txt` | 20 Markdown chapters under `environment/docs/opentype/`. |
| fontTools | https://github.com/fonttools/fonttools | `4.65.0` | Build-time reference only in the verifier image (`pip install fonttools==4.65.0`); not bundled in the task tree. |

The font corpus contains 4,359 `.ttf`/`.otf` files with 670 distinct contents
(names are the first 12 hexadecimal digits of the file's SHA-1). The same
corpus is copied into several step workdirs and `tests/corpus/` directories.

## 3. License map

| SPDX ID | File under `LICENSES/` | Component / version covered |
|---|---|---|
| FTL | `FTL.txt` | FreeType 2.14.3 (`VER-2-14-3`), the FreeType Project License, exact upstream `docs/FTL.TXT` |
| GPL-2.0-only | `GPL-2.0.txt` | FreeType's alternative license, exact upstream `docs/GPLv2.TXT`; also the text for the GPL-2.0-or-later Loma Minor font notice in section 5 |
| MIT | `MIT.txt` | HarfBuzz 14.4.0, exact upstream `COPYING` |
| BSD-3-Clause | `BSD-3-Clause.txt` | OTS v9.3.0, exact upstream `LICENSE` |
| OFL-1.1 | `OFL-1.1.txt` | 92 corpus fonts whose name table declares the SIL Open Font License 1.1 |
| Apache-2.0 | `Apache-2.0.txt` | 2 corpus fonts whose name table declares Apache-2.0 |
| CC0-1.0 | `CC0-1.0.txt` | 6 corpus fonts whose name table declares CC0-1.0 |
| Unicode-3.0 | `Unicode-3.0.txt` | Unicode Standard 17.0.0 chapter 3, exact `https://www.unicode.org/license.txt` |
| Unicode-3.0 | `Unicode-3.0-TRT.txt` | text-rendering-tests repository, exact upstream `LICENSE` (2016-2024) |
| W3C-Software-and-Document-License-2023 | `W3C-Software-and-Document-License-2023.txt` | W3C Compositing and Blending Level 1, CRD 2024-03-21 |
| MIT | `MIT-fonttools.txt` | fontTools, exact upstream `LICENSE`; covers the corpus font that links to the fontTools license |
| BSD-3-Clause | `BSD-3-Clause-MutatorMath.txt` | MutatorMath, exact upstream `LICENSE` (BSD-3-Clause); covers the corpus font that declares "same as MutatorMath" |
| — (font notice) | `Monotype-Test-Font-Notice.txt` | Monotype TestGVAROne/Two/Three fonts, exact embedded copyright and permission notice |
| — (font agreement) | `BuffaloGals-Font-Software-Evaluation-Agreement.txt` | Exact upstream `LICENSE.txt` for the Buffalo Gals font; see the restriction noted in section 5 |

The Microsoft OpenType chapters have no license text on Microsoft Learn: the
pages carry only links to Microsoft's Terms of Use and Trademarks pages. No
license text could be obtained for those files; they are listed in section 5
as an unresolved item.

## 4. Benchmark modifications

- The corpus fonts were renamed to the first 12 hex digits of their SHA-1 and
  copied into the per-step workdirs and test corpora. Font binary contents were
  not modified.
- Microsoft Learn Markdown downloads had their leading site-metadata front
  matter removed; the per-file SHA-256 in `environment/docs-manifest.txt` is of
  the shipped text.
- FreeType's error-code headers are shipped verbatim as documentation data.
- The implementation under each `steps/*/solution/tree/` is authored for the
  benchmark; it is not copied FreeType, HarfBuzz or OTS source.
- The verifier image builds FreeType 2.14.3, freetype-demos, HarfBuzz 14.4.0
  and OTS 9.3.0 from their pinned tags at image-build time; those sources are
  not part of the task tree.

## 5. Bundled third-party dependencies

| Name | Version | License | Bundled path |
|---|---|---|---|
| FreeType error-code headers | 2.14.3 | FTL OR GPL-2.0 | `environment/docs/freetype/fterrdef.h`, `fterrors.h` |
| HarfBuzz utilities manual page | 14.4.0 (site commit `d7d0dfe`) | MIT | `environment/docs/harfbuzz/utilities.html` |
| OpenType specification chapters | 1.9.1 | No license text published; Microsoft copyright | `environment/docs/opentype/*.md` (20 files) |
| Unicode Standard chapter 3 | 17.0.0 | Unicode-3.0 | `environment/docs/unicode/core-spec-17.0.0-chapter-3.html` |
| W3C Compositing and Blending Level 1 | CRD 2024-03-21 | W3C Software and Document License 2023 | `environment/docs/w3c/compositing-1-20240321.html` |
| Font corpus | mixed, see section 5 | OFL-1.1 / Apache-2.0 / CC0-1.0 / GPL-2.0-or-later / MIT / BSD-3-Clause / Unicode-3.0 / Monotype notice / Buffalo Gals agreement / unstated | `steps/*/workdir/*fonts/`, `steps/*/tests/corpus/` |

fontTools 4.65.0 is installed only while building the verifier image and is not
redistributed in this task tree.

### 5.1 Font provenance and notices

Provenance was reconstructed from the `file` / `source` pairs in the bundled
test JSON and from the SHA-1 filenames used by the OTS corpus:

- 176 distinct fonts match an OTS `tests/fonts/` path (94 named explicitly in
  test JSON, 82 more matched by SHA-1 filename prefix).
- 22 distinct fonts are named explicitly as HarfBuzz test fonts (18 under
  `test/api/fonts` or `test/shape/data/in-house/fonts`, 4 under
  `test/shape/data/text-rendering-tests/fonts`).
- 472 distinct fonts have no upstream path recorded in the task tree.

The 670 distinct fonts carry these embedded license declarations:

| Embedded declaration | Count | Notice |
|---|---|---|
| SIL Open Font License 1.1 | 92 | OFL-1.1 text in `LICENSES/OFL-1.1.txt`. Each font's own copyright line remains in its name table (76 families, 51 distinct copyright strings). Representative lines: "Copyright © 2018 Unicode, Inc."; "Copyright (c) 1998-2011 SIL International"; "Copyright 2016 Adobe Systems Incorporated"; "Copyright 2015 Google Inc. All Rights Reserved."; "Copyright 2016 The Cabin Project Authors". |
| Apache License 2.0 | 2 | Fonts `0f3ff74da736.ttf` ("Copyright 2011 Google Inc. All Rights Reserved.") and `942fab6918c0.ttf` ("Digitized data copyright (c) 2011, Google Corporation."); Apache-2.0 text in `LICENSES/Apache-2.0.txt`. |
| CC0-1.0 | 6 | Includes `05aee236755c.ttf` (Normschrift subset, "copyright 2017 by Jens Kutilek") and `074a5ae6b19d.ttf` ("Copyright (c) 2017, David Corbett"); CC0 text in `LICENSES/CC0-1.0.txt`. |
| GPL-2.0-or-later with font exception | 1 | Loma Minor, `1ccbe1de6cfd.ttf`: "Copyright (c) NECTEC, 2003. All rights reserved. Modified under GNU General Public License by TLWG." The embedded notice offers GPL-2.0-or-later and a document-embedding exception; GPL-2.0 text in `LICENSES/GPL-2.0.txt`. |
| Monotype test-font notice | 3 | TestGVAROne (`ca8e67b640ac.ttf`), TestGVARTwo (`8f0edeed0f62.ttf`), TestGVARThree (`ce69b6a9b11b.ttf`): "Copyright 2016 Monotype Hong Kong Ltd. and Monotype Imaging Inc. All rights reserved." The notice grants the Unicode Consortium and recipients permission to use, copy, modify, publish, distribute, sublicense and sell the font. Exact text in `LICENSES/Monotype-Test-Font-Notice.txt`. |
| Other linked license | 4 | `a53afc75de22.ttf` links to the fontTools license (MIT; `LICENSES/MIT-fonttools.txt`); `ec0f5f1cccb6.ttf` says "License same as MutatorMath. BSD 3-clause." (`LICENSES/BSD-3-Clause-MutatorMath.txt`); `6f850127b9ea.ttf` links to the text-rendering-tests license (`LICENSES/Unicode-3.0-TRT.txt`); `362dcdbe8625.ttf` links to the Buffalo Gals `LICENSE.txt` (`LICENSES/BuffaloGals-Font-Software-Evaluation-Agreement.txt`). |
| No license statement | 562 | No copyright or license field in the name table. No license text can be asserted for these files. |

Important restriction: the upstream Buffalo Gals `LICENSE.txt` is a "Font
Software Evaluation Agreement (no distribution rights)" and does not grant
redistribution rights. One corpus font (`362dcdbe8625.ttf`) points to that
agreement. The same typeface also appears as `6f850127b9ea.ttf` pointing to the
text-rendering-tests license (Unicode License V3), but the `362dcdbe8625.ttf`
copy's own declared terms are the restrictive agreement. This item should be
reviewed before redistribution.

The 472 fonts without a recorded upstream path include 408 with no license
statement, 52 declaring OFL-1.1, 6 CC0-1.0, 3 Monotype, 2 other-linked and 1
Apache-2.0. Their embedded notices, where present, are those listed above.

## 6. Required notices

- FreeType: the copyright and license notices are in `LICENSES/FTL.txt` and
  `LICENSES/GPL-2.0.txt` (Copyright 1996-2002, 2006 by David Turner, Robert
  Wilhelm, and Werner Lemberg, and the Free Software Foundation's GPL text).
- HarfBuzz: the copyright and MIT permission notice are in
  `LICENSES/MIT.txt`.
- OTS: the copyright and BSD-3-Clause notice are in
  `LICENSES/BSD-3-Clause.txt`.
- Unicode: "© 1991-2025 Unicode, Inc. Unicode and the Unicode Logo are
  registered trademarks of Unicode, Inc. in the U.S. and other countries."
  The license text in `LICENSES/Unicode-3.0.txt` carries the current
  "Copyright © 1991-2026 Unicode, Inc." notice; the text-rendering-tests copy
  carries "Copyright © 2016-2024 Unicode, Inc."
- W3C: "Copyright © 2024 World Wide Web Consortium." W3C® is a registered
  trademark of the World Wide Web Consortium; trademark rules apply.
- Microsoft: the OpenType chapters are Microsoft copyrighted material. No
  license text is published on the Microsoft Learn pages; the pages link to
  Microsoft's Terms of Use and Trademarks pages. Microsoft and Windows are
  trademarks of Microsoft Corporation.
- Fonts: each font's copyright and license strings remain embedded in its
  `name` table; representative lines are listed in section 5.

## 7. Closing note

Task-authored benchmark content is licensed separately by the benchmark maintainers.
