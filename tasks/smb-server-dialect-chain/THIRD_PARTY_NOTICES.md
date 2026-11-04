# Third-party notices — smb-server-dialect-chain

Carry one SMB file server from the SMB1/CIFS dialect to SMB 3.1.1, one dialect
generation per step, graded with Samba's own `smbtorture` and `smbclient`.

## Upstream projects

| | |
|---|---|
| Project | Microsoft Open Specifications |
| URL | https://learn.microsoft.com/openspecs/ |
| Documents and revisions | `MS-CIFS` v20251121; `MS-SMB` v20260114; `MS-NLMP` v20260330; `MS-SPNG` v20260714; `MS-DTYP` v20241119; `MS-FSCC` v20251121; `MS-ERREF` v20241119 |
| Bundled | Full plain-text editions of the seven documents under `environment/spec/`, copied to `/app/SPEC` |
| Source | `https://winprotocoldocs-bhdugrdyduf5h2e4.b02.azurefd.net/<DOC>/%5b<DOC>%5d.pdf`, retrieved 2026-09-12 and converted with `pdftotext -layout` (poppler-utils 22.12.0); `environment/spec/PROVENANCE.md` records the SHA-256 of every source PDF |
| Project | Samba |
| URL | https://www.samba.org/samba/docs/current/man-html/ |
| Documents | `smbd(8)`, `smb.conf(5)`, `smbpasswd(5)`, `smbclient(1)` |
| Bundled | Plain-text renderings under `environment/spec/samba-man/` (`html2text -width 100 -nobs`, retrieved 2026-09-12), plus `smb.conf-defaults.txt` read out of the Debian 4.17.12 build with `testparm -v` |
| Runtime packages | Debian bookworm `smbclient` and `samba-testsuite` 2:4.17.12+dfsg-0+deb12u4, installed by `environment/Dockerfile`; not copied into the task tree |
| Project | IETF |
| Documents | RFC 4178 (SPNEGO), RFC 2743 (GSS-API), RFC 4493 (AES-CMAC), RFC 4543 (GMAC) |
| Bundled | Unmodified text under `environment/docs/ietf/`, copied to `/opt/docs/ietf/`; per-file SHA-256 in `environment/docs-manifest.txt` |
| Project | NIST |
| Document | SP 800-108 (October 2009, withdrawn), `environment/docs/nist/sp800-108-2009.{pdf,txt}` |
| Project | The Open Group |
| Document | C706, *DCE 1.1: Remote Procedure Call*, HTML edition under `environment/docs/opengroup-c706/` |

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| Microsoft Open Specifications IPR notice (documentation terms, not an OSI license) | `LICENSES/Microsoft-Open-Specifications-IPR-Notice.txt` | MS-CIFS, MS-SMB, MS-NLMP, MS-SPNG, MS-DTYP, MS-FSCC, MS-ERREF |
| GPL-3.0-or-later | `LICENSES/GPL-3.0-or-later.txt` | Samba manual pages (`smbd(8)`, `smb.conf(5)`, `smbpasswd(5)`, `smbclient(1)`) and `smb.conf-defaults.txt` |
| IETF Trust / BCP 78 (in-document notice) | Appendix A below | RFC 4178, RFC 2743, RFC 4493, RFC 4543 |
| US Government work (public domain in the United States) | Appendix B below | NIST SP 800-108 (2009) |
| The Open Group copyright notice | `LICENSES/Open-Group-C706-Copyright-Notice.txt` | The Open Group C706 HTML edition |

`LICENSES/GPL-3.0-or-later.txt` is the `COPYING` file from Samba release
`samba-4.17.12` (SHA-256
`8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903`, identical
in `v4-17-stable` and current `master`). The "or later" option comes from
Samba's source headers, which offer "either version 3 of the License, or (at
your option) any later version"; the `COPYING` file itself is the GNU GPL
version 3 text.

## Microsoft Open Specifications IPR notice and the Open Specifications Promise

The full notice is reproduced in
`LICENSES/Microsoft-Open-Specifications-IPR-Notice.txt`, copied verbatim from
the front matter of MS-CIFS, where the same notice appears in every one of the
seven documents. Two distinct things are involved, and the notice keeps them
distinct:

- **Copyright.** The notice's "Copyrights" paragraph grants a limited right to
  copy the documentation to develop implementations of the described
  technologies and to distribute portions of it in those implementations or in
  documentation necessary to describe them. It also permits distributing
  schemas, IDLs and code samples from the documentation, with or without
  modification. This is a copyright permission, and it is what covers the
  text bundled here.
- **Patents.** The notice's "Patents" paragraph says that neither the notice
  nor Microsoft's delivery of the documentation grants any patent licences,
  and points to the Microsoft Open Specifications Promise (OSP) or the
  Microsoft Community Promise for documents that are covered by one of them.
  The OSP is a covenant not to sue over certain patent claims; it is not a
  copyright licence and grants no rights in the documentation text. A
  document being "covered by the OSP" therefore does not make it open source
  and does not license copying of its prose.

The task ships the documents under the copyright permission in the IPR notice;
it makes no patent-licence claim. The notice's "Trademarks", "Fictitious
Names" and "Reservation of Rights" paragraphs also apply.

## Version-to-document map

All six steps read the same `/app/SPEC` corpus; only the runtime Samba package
is version-pinned.

| Step | Dialect | Microsoft documents used | Samba documents used |
|---|---|---|---|
| `01-smb1-cifs` | SMB1 / CIFS | MS-CIFS, MS-SMB, MS-NLMP, MS-SPNG, MS-FSCC, MS-DTYP, MS-ERREF | `smbd(8)`, `smb.conf(5)`, `smbpasswd(5)`, `smbclient(1)` |
| `02-smb2-0-2-and-2-1` | SMB 2.0.2 / 2.1 | same seven | same four |
| `03-smb3-0` | SMB 3.0 | same seven | same four |
| `04-smb3-0-2-witness-acls` | SMB 3.0.2 | same seven | same four |
| `05-smb3-1-1` | SMB 3.1.1 | same seven | same four |
| `06-policy-quirks` | cross-dialect policy | same seven | same four |

## Modifications made by the benchmark

- Each Microsoft document is a `pdftotext -layout` rendering of the published
  PDF, unedited; page headers, footers and the IPR notice are retained.
- Samba manual pages were rendered from the published HTML with
  `html2text -width 100 -nobs`; prose is unedited.
- `environment/spec/samba-man/smb.conf-defaults.txt` is output of `testparm -v`
  from the Debian 4.17.12 build, not a Samba document.
- `environment/spec/PROVENANCE.md`, `environment/spec/SERVICE_INTERFACE.md`
  and `environment/spec/conf/` are authored by the task; they state how the
  service is started and configured and add nothing to the protocol.
- RFCs and NIST SP 800-108 are copied unmodified. The C706 HTML pages are
  copied unmodified.
- No Microsoft, Samba, IETF, NIST or Open Group source code is bundled. The
  server implementation, harnesses, tests other than the Samba clients invoked
  at run time, and instructions are authored benchmark content.

## Third-party dependencies bundled in the task

| Component | Version | License | Bundled path |
|---|---|---|---|
| Microsoft Open Specifications | revisions listed above | Microsoft IPR notice | `environment/spec/MS-*.txt` |
| Samba manual pages | `current` as of 2026-09-12 | GPL-3.0-or-later | `environment/spec/samba-man/*.txt` |
| Samba `testparm -v` defaults | Debian 4.17.12+dfsg-0+deb12u4 | GPL-3.0-or-later (output) | `environment/spec/samba-man/smb.conf-defaults.txt` |
| IETF RFC 4178, 2743, 4493, 4543 | as published | IETF Trust / BCP 78 | `environment/docs/ietf/` |
| NIST SP 800-108 | October 2009 | US Government work | `environment/docs/nist/` |
| The Open Group C706 | 1997 edition | Open Group copyright notice | `environment/docs/opengroup-c706/` |

## Required notices

Microsoft:

> Copyright (c) 2025 Microsoft Corporation

Each document's title page carries its own year and revision line (for
example, MS-CIFS v20251121 is "Copyright (c) 2025 Microsoft Corporation";
MS-DTYP v20241119 and MS-ERREF v20241119 carry the 2024 line).

Samba:

> The original Samba software and related utilities were created by Andrew
> Tridgell. Samba is now developed by the Samba Team as an Open Source project
> similar to the way the Linux kernel is developed.

Microsoft, Windows, Windows NT and other Microsoft product names are
trademarks or registered trademarks of Microsoft Corporation. Samba is a
trademark of the Samba Team. This task is not affiliated with or endorsed by
Microsoft Corporation or the Samba Team.

## Appendix A — IETF RFC notices

Every bundled RFC carries its own "Status of This Memo" and "Copyright
Notice" in the file. The four notices read:

- RFC 4178 (October 2005), RFC 4493 (June 2006) and RFC 4543 (August 2006):
  "Copyright (C) The Internet Society (2005)." or "Copyright (C) The Internet
  Society (2006)." Each "Full Copyright Statement" states that the document
  "is subject to the rights, licenses and restrictions contained in BCP 78,
  and except as set forth therein, the authors retain all their rights", and
  provides the information on an "AS IS" basis with the IETF and Internet
  Society disclaiming all warranties.
- RFC 2743 (January 2000): "Copyright (C) The Internet Society (2000). All
  Rights Reserved." Its "Full Copyright Statement" permits copying and
  furnishing the document, and preparing derivative works that comment on or
  otherwise explain it or assist in its implementation, provided the
  copyright notice and that paragraph are included; the document itself may
  not be modified except as needed for Internet standards development or
  translation. The permissions are perpetual and not revoked by the Internet
  Society or its successors.

The complete statements are in the corresponding files under
`environment/docs/ietf/`.

## Appendix B — NIST SP 800-108 notice

The bundled text begins with NIST's "Withdrawn NIST Technical Series
Publication — Warning Notice", which records the withdrawal date of
2022-08-17 and the superseding publication NIST SP 800-108r1. NIST
publications are works of the United States Government and carry no
copyright in the United States (17 U.S.C. § 105); NIST requests
acknowledgement of its contribution.

## Appendix C — The Open Group C706 notice

Reproduced in `LICENSES/Open-Group-C706-Copyright-Notice.txt`:

> ©October 1997, The Open Group
> All rights reserved.
> No part of this publication may be reproduced, stored in a retrieval system,
> or transmitted, in any form or by any means, electronic, mechanical,
> photocopying, recording or otherwise, without the prior permission of the
> copyright owners.
>
> This document and the software to which it relates are derived in part from
> materials which are copyright © 1990, 1991 Digital Equipment Corporation and
> copyright © 1990, 1991 Hewlett-Packard Company.

The HTML files carry the line "Copyright 1997 The Open Group, All rights
reserved" in a comment and "Copyright © 1997 The Open Group" on each page.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
