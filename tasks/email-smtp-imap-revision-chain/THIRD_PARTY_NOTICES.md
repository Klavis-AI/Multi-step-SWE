# Third-Party Notices

Task: `email-smtp-imap-revision-chain` — a ten-step C++ MTA and IMAP/JMAP
stack carried through the SMTP/IMAP RFC revision chain, using Postfix 3.8.6
and Dovecot 2.3.21 configuration and Maildir storage as the reference
behaviour.

## Upstream projects and pins

| item | value |
| --- | --- |
| Postfix | https://www.postfix.org/ — version 3.8.6, release date 2024-03-04 (`MAIL_RELEASE_DATE` in `src/global/mail_version.h`). Reference documentation only. |
| Postfix archive | `https://ftp.porcupine.org/mirrors/postfix-release/official/postfix-3.8.6.tar.gz`, SHA-256 `4b6e17c826cc438cc3016a9c0a55ea7e77c6cbafba7dd57241d81b690b0e9774` |
| Dovecot | https://dovecot.org/ — version 2.3.21 (2023-09-14). Reference documentation only. |
| Dovecot archive | `https://dovecot.org/releases/2.3/dovecot-2.3.21.tar.gz`, SHA-256 `05b11093a71c237c2ef309ad587510721cc93bbee6828251549fc1586c36502d` |
| RFCs | RFC Editor texts, https://www.rfc-editor.org/ — 49 unique RFCs bundled as plain text |
| IETF policy | IETF Trust Legal Provisions (TLP), current corrected version effective 2015-03-25, https://trustee.ietf.org/documents/trust-legal-provisions/tlp-5/ ; BCP 78 is RFC 5378 |
| What is bundled/derived | Postfix and Dovecot documentation only. No Postfix or Dovecot source code is stored in the task tree or copied into the image. The step workspaces also carry RFC texts and one D. J. Bernstein Maildir article. All C++ source, tests and fixtures are benchmark-authored. |

`environment/docs-provenance.txt` records the exact fetch and conversion
commands for `environment/docs/`; `environment/docs/SHA256SUMS` records the
resulting files.

## License table

| SPDX ID | File under `LICENSES/` | Covers |
| --- | --- | --- |
| EPL-2.0 | `EPL-2.0.txt` | Postfix license option 1. Exact Eclipse Public License - v 2.0 portion of the upstream `LICENSE` (lines 10-286 of the 508-line file), including Exhibit A. |
| IPL-1.0 | `IPL-1.0.txt` | Postfix license option 2. Exact IBM Public License Version 1.0 portion of the upstream `LICENSE` (lines 288-508). |
| EPL-2.0 OR IPL-1.0 (upstream file) | `Postfix-LICENSE.txt` | Exact upstream Postfix 3.8.6 `LICENSE`, including the dual-license statement and both license texts. |
| MIT | `MIT.txt` | Dovecot `src/lib/`, `src/auth/` and `src/lib-sql/` per `COPYING`; exact upstream `COPYING.MIT`. Also one of the two licenses named by the bundled `dovecot.1` manual page. |
| LGPL-2.1 | `LGPL-2.1.txt` | Dovecot components not otherwise marked, including the bundled wiki, example configuration and manual page; exact upstream `COPYING.LGPL`. Also named by `dovecot.1`. |
| LGPL-2.1 (project statement) | `Dovecot-COPYING.txt` | Exact upstream `COPYING`, which states the per-directory split and the source-file exceptions. |
| IETF Trust TLP-5 | `IETF-Trust-Legal-Provisions-TLP-5.txt` | IETF documents published under the Trust Legal Provisions effective 2015-03-25. |
| BCP 78 | `RFC5378.txt` | RFC 5378, "Rights Contributors Provide to the IETF Trust", the BCP 78 framework referenced by the RFC copyright notices. |

Dovecot's `COPYING` also reproduces the BSD-3-Clause terms for
`src/lib/sha1.c` and `sha2.c` and the Unicode terms for
`src/lib/UnicodeData.txt`, and records `src/lib/md5.c` as public domain.
Those source files are not part of the documentation subset bundled here.
Postfix's separate upstream `COPYRIGHT` file covers the
BSD-4-Clause-licensed `fix_strcasecmp.c` Solaris workaround module; that
module is not part of the documentation subset bundled here.

## Bundled documentation by step

Each step's `workdir/SPEC/` carries only the RFC texts that step needs. The
five files in `environment/docs/rfc/` are additional reference texts available
to the agent at `/opt/docs/rfc/`.

| step | bundled RFCs |
| --- | --- |
| 01-rfc821-imap4rev1 | RFC 821, 822, 974, 3501; `maildir.html` |
| 02-rfc2821-2822 | RFC 2821, 2822 |
| 03-rfc5321-submission | RFC 5321, 5322, 6409 |
| 04-esmtp-extensions | RFC 1652, 1870, 2920, 3030, 3207, 3461, 3463, 4954, 5802 |
| 05-imap-extensions | RFC 2088, 2177, 2342, 3516, 4315, 4731, 6851, 7162 |
| 06-eai-smtputf8 | RFC 6530, 6531, 6532, 6533, 6855 |
| 07-auth-spf-dkim-dmarc | RFC 6376, 7208, 7489 |
| 08-arc-sieve | RFC 5228, 5804, 8617 |
| 09-mta-sts-dane | RFC 7672, 8460, 8461, 8689 |
| 10-imap4rev2-jmap | RFC 8620, 8621, 9051 |
| `environment/docs/rfc/` | RFC 1035, 2047, 6698, 7617, 8601 |

## RFC copyright notices

The RFC texts are reproduced in full, so each document carries its own
copyright and license notice. The notices fall into these groups.

| notice | RFCs | exact copyright line or statement |
| --- | --- | --- |
| No copyright or license statement | RFC 821, 822 | Neither text contains a copyright, license or distribution statement. No license is asserted for them here. |
| Distribution unlimited only | RFC 974, 1035, 1652, 1870, 2047, 2088, 2177 | The status-of-memo section says `Distribution of this memo is unlimited.` No other license text appears in the document. |
| Internet Society copyright | RFC 2342 (1998), 2920 (2000), 3030 (2000), 2821 (2001), 2822 (2001), 3207 (2002), 3461 (2003), 3463 (2003), 3501 (2003), 3516 (2003), 4315 (2005) | `Copyright (C) The Internet Society (YEAR).  All Rights Reserved.` followed by the ISOC Full Copyright Statement reproduced in Appendix A. RFC 4315's line omits `All Rights Reserved.` and reads `Copyright (C) The Internet Society (2005).` |
| IETF Trust pre-BCP 78 statement | RFC 4731 (2006), 4954 (2007) | `Copyright (C) The IETF Trust (YEAR).` followed by the statement reproduced in Appendix B. |
| IETF Trust with BCP 78 | RFC 5228, 5321, 5322 (2008); 5802, 5804 (2010); 6376, 6409 (2011); 6530, 6531, 6532, 6533, 6698 (2012); 6851, 6855 (2013); 7162, 7208 (2014) | `Copyright (c) YEAR IETF Trust and the persons identified as the document authors. All rights reserved.` plus the BCP 78 reference reproduced in Appendix C. Several of these documents also carry the standard paragraph about pre-5378 material. |
| IETF Trust with BCP 78 (2015 and later) | RFC 7489, 7617, 7672 (2015); 8460, 8461 (2018); 8601, 8617, 8620, 8621, 8689 (2019); 9051 (2021) | `Copyright (c) YEAR IETF Trust and the persons identified as the document authors. All rights reserved.` plus the BCP 78 reference to `https://trustee.ietf.org/license-info`. |

For documents published before the TLP-5 effective date of 2015-03-25, the
IETF TLP says the licensing provisions in effect at the time of publication
continue to apply; their full notices are reproduced in the documents
themselves and in Appendices A and B. The TLP-5 text in `LICENSES/` is the
current version and applies to documents published after that date.

## Modifications made by the benchmark

- `environment/docs/postfix-3.8.6/` contains six HTML pages (`postfix.1`,
  `postconf.5`, `master.5`, `transport.5`, `virtual.8`, `DATABASE_README`)
  extracted from the `html/` directory of the Postfix 3.8.6 release tarball.
  No Postfix source or binary is copied.
- `environment/docs/dovecot-2.3.21/` contains the `doc/wiki` text files, the
  `doc/example-config` tree with `Makefile.*` removed, and `dovecot.1.txt`
  rendered from `doc/man/dovecot.1.in` with `@pkgsysconfdir@` replaced by
  `/etc/dovecot` and with `groff -t -man -Tutf8`. No Dovecot source or binary
  is copied.
- The RFC texts are stored as plain text in `steps/*/workdir/SPEC/` and
  `environment/docs/rfc/`; no license or copyright notice was removed or
  altered.
- `maildir.html` is a copy of D. J. Bernstein's "Using maildir format" page,
  with no license notice added or removed.
- No upstream license file is modified or removed.

## Third-party dependencies bundled in the task

The task bundles documentation only. It does not vendor or link any third-party
library, and does not install Postfix or Dovecot in the task image. The
Postfix and Dovecot licenses above cover the documentation files that were
extracted from their release tarballs; the C++ source, tests and fixtures are
benchmark-authored. The Debian packages and Node.js runtime installed by
`environment/Dockerfile` are supplied by the image, not stored in the task
tree, and their own distribution licenses apply.

## Required notices

- Postfix: the exact upstream `LICENSE` begins `LICENSE - SECURE MAILER` and
  states that the software is dual-licensed under the Eclipse Public License
  version 2.0 and the IBM Public License version 1.0, with the recipient free
  to choose. Both texts are in `LICENSES/`. The upstream `LICENSE` carries no
  copyright line; Postfix is authored by Wietse Venema and contributors. The
  bundled HTML documentation pages carry no separate copyright notice.
- Dovecot: the bundled `dovecot.1` page states: "Dovecot <http://dovecot.org>
  and its manual pages were written by the Dovecot authors
  <http://dovecot.org/doc/AUTHORS>, mainly Timo Sirainen <tss at iki.fi>, and
  are licensed under the terms of the MIT and LGPLv2.1 licenses, see
  <http://dovecot.org/doc/COPYING> for details." The `COPYING` file begins
  `See AUTHORS file for list of copyright holders.` The release's `AUTHORS`
  file begins with `Timo Sirainen <tss@iki.fi>` and includes
  `Portions Copyright (c) 2008 Apple Inc.  All rights reserved.`
- IETF: the per-document copyright lines are tabulated above. The full ISOC,
  IETF Trust pre-BCP 78 and BCP 78 statements are reproduced in the appendices
  below. The IETF Trust's current Legal Provisions are in
  `IETF-Trust-Legal-Provisions-TLP-5.txt`; BCP 78 is `RFC5378.txt`.
- `maildir.html` (D. J. Bernstein): no copyright or license statement appears
  in the page. No license is asserted for it and no text is fabricated.
- Postfix, Dovecot, IETF, IMAP, SMTP, JMAP and Maildir are names or trademarks
  of their respective owners. This task claims no endorsement by the Postfix
  project, Dovecot Oy, the IETF Trust, the Internet Society or D. J. Bernstein.

## Appendix A — Internet Society Full Copyright Statement

Exact text as it appears in RFC 2821 (the copyright year varies by RFC, as
listed in the table above):

```text
Full Copyright Statement

   Copyright (C) The Internet Society (2001).  All Rights Reserved.

   This document and translations of it may be copied and furnished to
   others, and derivative works that comment on or otherwise explain it
   or assist in its implementation may be prepared, copied, published
   and distributed, in whole or in part, without restriction of any
   kind, provided that the above copyright notice and this paragraph are
   included on all such copies and derivative works.  However, this
   document itself may not be modified in any way, such as by removing
   the copyright notice or references to the Internet Society or other
   Internet organizations, except as needed for the purpose of
   developing Internet standards in which case the procedures for
   copyrights defined in the Internet Standards process must be
   followed, or as required to translate it into languages other than
   English.

   The limited permissions granted above are perpetual and will not be
   revoked by the Internet Society or its successors or assigns.

   This document and the information contained herein is provided on an
   "AS IS" basis and THE INTERNET SOCIETY AND THE INTERNET ENGINEERING
   TASK FORCE DISCLAIMS ALL WARRANTIES, EXPRESS OR IMPLIED, INCLUDING
   BUT NOT LIMITED TO ANY WARRANTY THAT THE USE OF THE INFORMATION
   HEREIN WILL NOT INFRINGE ANY RIGHTS OR ANY IMPLIED WARRANTIES OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
```

## Appendix B — IETF Trust pre-BCP 78 statement

Exact text as it appears in RFC 4731 (the copyright year varies by RFC):

```text
Full Copyright Statement

   Copyright (C) The IETF Trust (2006).

   This document is subject to the rights, licenses and restrictions
   contained in BCP 78, and except as set forth therein, the authors
   retain all their rights.

   This document and the information contained herein are provided on an
   "AS IS" basis and THE CONTRIBUTOR, THE ORGANIZATION HE/SHE REPRESENTS
   OR IS SPONSORED BY (IF ANY), THE INTERNET SOCIETY, THE IETF TRUST,
   AND THE INTERNET ENGINEERING TASK FORCE DISCLAIM ALL WARRANTIES,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTY THAT
   THE USE OF THE INFORMATION HEREIN WILL NOT INFRINGE ANY RIGHTS OR ANY
   IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR
   PURPOSE.
```

## Appendix C — IETF Trust BCP 78 statement

Exact text as it appears in RFC 5321 (the copyright year varies by RFC):

```text
Full Copyright Statement

   Copyright (C) The IETF Trust (2008).

   This document is subject to the rights, licenses and restrictions
   contained in BCP 78, and except as set forth therein, the authors
   retain all their rights.

   This document and the information contained herein are provided on an
   "AS IS" basis and THE CONTRIBUTOR, THE ORGANIZATION HE/SHE REPRESENTS
   OR IS SPONSORED BY (IF ANY), THE INTERNET SOCIETY, THE IETF TRUST AND
   THE INTERNET ENGINEERING TASK FORCE DISCLAIM ALL WARRANTIES, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTY THAT THE USE OF
   THE INFORMATION HEREIN WILL NOT INFRINGE ANY RIGHTS OR ANY IMPLIED
   WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
```

Task-authored benchmark content is licensed separately by the benchmark maintainers.
