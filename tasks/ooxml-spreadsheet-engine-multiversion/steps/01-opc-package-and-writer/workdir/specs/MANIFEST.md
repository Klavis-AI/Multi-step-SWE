# Spec corpus manifest — OOXML SpreadsheetML multiversion benchmark

All files shipped verbatim as downloaded. No document was converted, edited, or re-rendered.
Assembled 2026-09-20.

## Shipped files

| Path (relative to spec-corpus/) | Size (bytes) | SHA-256 | Source URL | Licence note |
|---|---|---|---|---|
| `ecma-376-part1/Ecma Office Open XML Part 1 - Fundamentals And Markup Language Reference.pdf` | 35304993 | 7cccfd0ad0e2ef89316acece15e44a8a088d0603ce4bba4e7b0361b50d13a37d | ECMA-376-1 zip (below) | Ecma copyright licence + disclaimer (see "Licences") — redistribution permitted verbatim |
| `ecma-376-part1/OfficeOpenXML-DrawingMLGeometries.zip` | 51672 | a8517838a3a60492d267d1ff92e1ebd831adc04584de79a38a5532a68902abf6 | ECMA-376-1 zip | same |
| `ecma-376-part1/OfficeOpenXML-RELAXNG-Strict.zip` | 95533 | 380c2a76210e48479ccb6eda79858cf394524534d54c51fcab5433a86c2dc99f | ECMA-376-1 zip | same |
| `ecma-376-part1/OfficeOpenXML-SpreadsheetMLStyles.zip` | 83776 | bfe5b222db0c12494350b6c36cf0c24ebad3c8ef0cd470d18a0d96a316bbc0c9 | ECMA-376-1 zip | same |
| `ecma-376-part1/OfficeOpenXML-XMLSchema-Strict.zip` | 93849 | 73fab4b04ddd93da020ed07583f755466dcaf69f115b2ff6649af7ae16a816f7 | ECMA-376-1 zip | same |
| `ecma-376-part2/Ecma Office Open XML Part 2 - Open Packaging Conventions.pdf` | 2009726 | 18701071fe15f39389761f82512c70d2effbf22bd16dd034f2939797ff5f6147 | ECMA-376-2 zip | same |
| `ecma-376-part2/OpenPackagingConventions-RELAXNG.zip` | 2575 | 5e7992d3a681055cf21873d1746fa228c5b4d594a7a05d357421e694549ffe98 | ECMA-376-2 zip | same |
| `ecma-376-part2/OpenPackagingConventions-XMLSchema.zip` | 3037 | e5a0a7c1f43ac8b8f66d850f48545c2ead09f6d632bb8539fab79962c7fd55df | ECMA-376-2 zip | same |
| `ecma-376-part3/Ecma Office Open XML Part 3 - Markup Compatibility and Extensibility.pdf` | 864652 | 3742ae2573fcf2eec6878ee345bf540541b77df47f09a57603851ca2d02dfa13 | ECMA-376-3 zip | Ecma notice embedded on p. viii of this PDF (quoted below) |
| `openformula/OpenDocument-v1.3-os-part4-formula.html` | 1819524 | 502dd637321794cfe46e8ad7d98e62b31751c85e8feac5ec19883ab563ade3cc | https://docs.oasis-open.org/office/OpenDocument/v1.3/os/part4-formula/OpenDocument-v1.3-os-part4-formula.html | OASIS Open 2021, copying/redistribution permitted, no modification |
| `ms-offcrypto/MS-OFFCRYPTO.pdf` | 3850547 | 65a20fdaef2b24cabd0c783620a46d339a98b33effffa9ca6da7795e9635ddf0 | https://officeprotocoldocs-f5hpbjgea6b8gneq.b02.azurefd.net/files/MS-OFFCRYPTO/%5bMS-OFFCRYPTO%5d.pdf | Microsoft Open Specifications IPR notice — copying permitted to develop implementations |

### Extracted but not shipped in this workspace

One file from the ECMA-376-1 archive is deliberately left out of `specs/`:
`ecma-376-part1/OfficeOpenXML-WordprocessingMLArtBorders.zip` (14425345 bytes, sha256 `f9894d9a26f7b59ba2b233fe87930c9f0a1ec17967fe66f3d2ddf83b79c631ff`).  It is 14 MB of WordprocessingML border
artwork for a markup language this product does not implement, and nothing
in this benchmark refers to it.  Every other file in the table above is
present under `specs/` at the size and digest recorded there.

## Source archives (downloaded, extracted, not shipped in spec-corpus/)

| Archive | Size (bytes) | SHA-256 | URL |
|---|---|---|---|
| ECMA-376-1_5th_edition_december_2016.zip | 43501721 | 9d0bcad9cf06054785b03762fcfadbf6bab7e54a5f9d69434e34b7fd464d4129 | https://ecma-international.org/wp-content/uploads/ECMA-376-1_5th_edition_december_2016.zip |
| ECMA-376-2_5th_edition_december_2021.zip | 1899204 | 1d489dc491168ea1f9e9a59063acc8dd5f02b4ad1d21aa7ec19ba9a58d020c70 | https://ecma-international.org/wp-content/uploads/ECMA-376-2_5th_edition_december_2021.zip |
| ECMA-376-3_5th_edition_december_2015.zip | 741741 | 42294159fbbbe9393ccadac95b859d7729cc68d908898bcbe31034dda059daa8 | https://ecma-international.org/wp-content/uploads/ECMA-376-3_5th_edition_december_2015.zip |

## Extracted sizes (du -sb)

| Directory | Bytes |
|---|---|
| ecma-376-part1 | 50059264 |
| ecma-376-part2 | 2019434 |
| ecma-376-part3 | 868748 |
| openformula | 1823620 |
| ms-offcrypto | 3854643 |
| **total (5 dirs)** | **58625709** |
| spec-corpus total incl. MANIFEST.md | ~58.64 MB (5 dirs = 58625709 + manifest) |
| ecma-376-part1 WITHOUT main PDF | 14754271 |

## Licences

### Ecma International (applies to ECMA-376 Parts 1, 2, 3)
Extracted verbatim from the COPYRIGHT NOTICE page (p. viii) of the Part 3 PDF. The Part 1
(2016) and Part 2 (2021) PDFs do not carry an extractable copyright page (page 2 of each is
blank: no text, no fonts, no image XObjects); the same Ecma boilerplate governs them.

> COPYRIGHT NOTICE
> (c) 2015 Ecma International
> This document may be copied, published and distributed to others, and certain derivative
> works of it may be prepared, copied, published, and distributed, in whole or in part,
> provided that the above copyright notice and this Copyright License and Disclaimer are
> included on all such copies and derivative works. The only derivative works that are
> permissible under this Copyright License and Disclaimer are:
> (i) works which incorporate all or portion of this document for the purpose of providing
> commentary or explanation (such as an annotated version of the document),
> (ii) works which incorporate all or portion of this document for the purpose of
> incorporating features that provide accessibility,
> (iii) translations of this document into languages other than English and into different
> formats and
> (iv) works by making use of this specification in standard conformant products by
> implementing (e.g. by copy and paste wholly or partly) the functionality therein.
> However, the content of this document itself may not be modified in any way, including by
> removing the copyright notice or references to Ecma International, except as required to
> translate it into languages other than English or into a different format.
> The official version of an Ecma International document is the English language version on
> the Ecma International website. In the event of discrepancies between a translated version
> and the official version, the official version shall govern.
> The limited permissions granted above are perpetual and will not be revoked by Ecma
> International or its successors or assigns.
> This document and the information contained herein is provided on an "AS IS" basis and
> ECMA INTERNATIONAL DISCLAIMS ALL WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
> TO ANY WARRANTY THAT THE USE OF THE INFORMATION HEREIN WILL NOT INFRINGE ANY OWNERSHIP
> RIGHTS OR ANY IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.

**Verdict: redistributable verbatim, YES.** Do not modify the documents.

### OASIS (applies to OpenFormula / ODF 1.3 Part 4)
From the "Notices" block of the HTML document:

> Copyright (c) OASIS Open 2021. All Rights Reserved.
> All capitalized terms in the following text have the meanings assigned to them in the
> OASIS Intellectual Property Rights Policy (the "OASIS IPR Policy"). The full Policy may be
> found at the OASIS website.
> This document and translations of it may be copied and furnished to others, and derivative
> works that comment on or otherwise explain it or assist in its implementation may be
> prepared, copied, published, and distributed, in whole or in part, without restriction of
> any kind, provided that the above copyright notice and this section are included on all
> such copies and derivative works. However, this document itself may not be modified in any
> way, including by removing the copyright notice or references to OASIS, except as needed
> for the purpose of developing any document or deliverable produced by an OASIS Technical
> Committee (in which case the rules applicable to copyrights, as set forth in the OASIS IPR
> Policy, must be followed) or as required to translate it into languages other than English.
> The limited permissions granted above are perpetual and will not be revoked by OASIS or its
> successors or assigns.
> This document and the information contained herein is provided on an "AS IS" basis and
> OASIS DISCLAIMS ALL WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY
> WARRANTY THAT THE USE OF THE INFORMATION HEREIN WILL NOT INFRINGE ANY OWNERSHIP RIGHTS OR
> ANY IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.

**Verdict: redistributable verbatim, YES.** Do not modify.

### Microsoft Open Specifications (applies to [MS-OFFCRYPTO])
From the "Intellectual Property Rights Notice for Open Specifications Documentation" on
page 1 of the PDF and on the Learn landing page:

> Copyrights. This documentation is covered by Microsoft copyrights. Regardless of any other
> terms that are contained in the terms of use for the Microsoft website that hosts this
> documentation, you can make copies of it in order to develop implementations of the
> technologies that are described in this documentation and can distribute portions of it in
> your implementations that use these technologies or in your documentation as necessary to
> properly document the implementation. You can also distribute in your implementation, with
> or without modification, any schemas, IDLs, or code samples that are included in the
> documentation.
> No Trade Secrets. Microsoft does not claim any trade secret rights in this documentation.
> Patents. Microsoft has patents that might cover your implementations of the technologies
> described in the Open Specifications documentation. Neither this notice nor Microsoft's
> delivery of this documentation grants any licenses under those patents or any other
> Microsoft patents. However, a given Open Specifications document might be covered by the
> Microsoft Open Specifications Promise or the Microsoft Community Promise.
> Reservation of Rights. All other rights are reserved, and this notice does not grant any
> rights other than as specifically described above, whether by implication, estoppel, or
> otherwise.

**Verdict: redistributable for implementation-development purposes, YES (qualified).** The
grant is purpose-bound ("in order to develop implementations"), which this benchmark corpus
satisfies. Schemas/IDL/code samples may be redistributed with or without modification.

## Document inventory notes

- **ECMA-376 Part 1** main document: `Ecma Office Open XML Part 1 - Fundamentals And Markup
  Language Reference.pdf`, 35304993 bytes, **PDF 1.x, 5039 pages**. Not a .docx. The ZIP
  contains **no** plain-text or HTML rendition and no separate notice/README file.
- Part 1 XSD schemas live inside `OfficeOpenXML-XMLSchema-Strict.zip`: 21 `.xsd` files,
  855792 bytes uncompressed (incl. `sml.xsd` 244208 B — SpreadsheetML — and
  `shared-commonSimpleTypes.xsd` 4831 B). RELAX NG equivalents: 86 `.rnc` files, 439639 B.
- Part 2 main document: PDF, 2009726 bytes, 95 pages. Schemas: 4 `.xsd` (8852 B) + 5 `.rnc`
  (5470 B).
- Part 3 main document: PDF, 864652 bytes. Sole member of its ZIP; no schemas.
- MS-OFFCRYPTO PDF: v20260217 (revision 14.0, released 2026-02-17), 119 pages.
- OpenFormula HTML: single file, one inline `<style>`, 1 inline `data:` SVG image; two
  external `<script>` refs (MathJax `mml-svg.js`, Cloudflare `email-decode.min.js`) — see
  report for self-containment caveat.
