# Third-Party Notices

Task: `dwarf-debugger-version-chain` — a five-step DWARF task carrying one
debug-information toolkit written in Java across DWARF 2/3, DWARF 4 and
DWARF 5, and matching the output of the real tools at each milestone.

## Upstream projects and pins

| item | value |
| --- | --- |
| DWARF specifications | https://dwarfstd.org/ — DWARF 2.0.0 (27 July 1993), DWARF 3 (20 December 2005), DWARF 4 (10 June 2010), DWARF 5 (13 February 2017) |
| LLVM documentation | LLVM 19.1.0, `llvm-symbolizer` command guide, https://llvm.org/docs/CommandGuide/llvm-symbolizer.html |
| binutils documentation | binutils 2.44, `readelf` manual, https://sourceware.org/binutils/docs/binutils/readelf.html |
| GDB documentation | GDB 16.3 (2025-04-20): `gdbserver(1)`, the GDB Remote Serial Protocol appendix and the Target Descriptions appendix from `gdb.info` |
| Itanium C++ ABI | https://github.com/itanium-cxx-abi/cxx-abi, `abi.html` |
| gABI | System V Application Binary Interface, generic part, https://refspecs.linuxfoundation.org/elf/gabi4+/contents.html |
| x86-64 psABI | System V ABI AMD64 Architecture Processor Supplement v1.0, 28 January 2018 |
| Arm ABI | AArch64 ABI release 2024Q3: `aadwarf64`, `aaelf64`, `aapcs64`, https://github.com/ARM-software/abi-aa |
| man-pages | Linux man-pages 6.9.1: `ptrace.2`, `wait.2`, `fork.2`, `execve.2`, `exec.3`, `personality.2`, `core.5`, `elf.5` |
| What is bundled/derived | DWARF 2/3/4/5 specification text in `steps/*/workdir/spec/`; the vendor documentation above in `environment/docs/`. The Java toolkit, its tests and its fixtures are benchmark-authored. No upstream source code is compiled into the task. |

## License table

| SPDX ID | File under `LICENSES/` | Covers |
| --- | --- | --- |
| GFDL-1.2 | `GFDL-1.2.txt` | DWARF 3 specification, copyright © 2005 Free Standards Group; no Invariant Sections, no Front-Cover Texts, no Back-Cover Texts |
| GFDL-1.3 | `GFDL-1.3.txt` | DWARF 4 and DWARF 5 specifications; binutils 2.44 `readelf` manual; GDB 16.3 documentation |
| CC-BY-SA-4.0 | `CC-BY-SA-4.0.txt` | Arm AArch64 ABI documents, release 2024Q3 |
| CC-BY-SA-4.0 with Arm patent license | `CC-BY-SA-4.0-Arm-Patent-License.txt` | The additional patent grant in the Arm ABI documents |
| Apache-2.0 WITH LLVM-exception | `Apache-2.0-WITH-LLVM-exception.txt` | LLVM 19.1.0 `llvm-symbolizer` documentation |
| Apache-2.0 WITH LLVM-exception (upstream file) | `LLVM-LICENSE.txt` | Verbatim LLVM `LICENSE.TXT` at the 19.1.0 documentation build, including the legacy LLVM license section for third-party components |
| GPL-2.0-or-later | `GPL-2.0.txt` | `ptrace.2` (`SPDX-License-Identifier: GPL-2.0-or-later`) |
| Linux-man-pages-copyleft | `Linux-man-pages-copyleft.txt` | `wait.2`, `fork.2`, `execve.2`, `personality.2`, `core.5` |
| BSD-4-Clause-UC | `BSD-4-Clause-UC.txt` | `exec.3` (`SPDX-License-Identifier: BSD-4-Clause-UC`), copyright (c) 1991 The Regents of the University of California |
| BSD-2-Clause (man-pages `PERMISSIVE_MISC`) | `BSD-2-Clause.txt` | `elf.5`, tagged `PERMISSIVE_MISC`; the text is the 2-clause BSD form, copyright (c) 1999 Jeroen Ruigrok van der Werven |
| UNIX International notice | `DWARF-2.0-Notice.txt` | DWARF 2.0.0, copyright © 1992, 1993 UNIX International, Inc.; permission to use, copy, modify and distribute with the notice preserved |

## Copyright and license notices per document

### DWARF specifications

- DWARF 2.0.0: `Copyright © 1992, 1993 UNIX International, Inc.` The exact
  permission and disclaimer text is `LICENSES/DWARF-2.0-Notice.txt`.
- DWARF 3: `Copyright © 2005 Free Standards Group`. Licensed under the GNU Free
  Documentation License, Version 1.2; with no Invariant Sections, with no
  Front-Cover Texts, and with no Back-Cover Texts.
- DWARF 4: `Copyright © 2010 DWARF Debugging Information Format Committee`.
  Licensed under the GNU Free Documentation License, Version 1.3; with no
  Invariant Sections, with no Front-Cover Texts, and with no Back-Cover Texts.
- DWARF 5: `Copyright © 2005, 2010, 2016, 2017 DWARF Debugging Information
  Format Committee`. Licensed under the GNU Free Documentation License,
  Version 1.3; with no Invariant Sections, with no Front-Cover Texts, and with
  no Back-Cover Texts.

### Vendor documentation

- LLVM 19.1.0: `© Copyright 2003-2024, LLVM Project.` Licensed under the
  Apache License v2.0 with LLVM Exceptions. The upstream `LICENSE.TXT` is
  included verbatim as `LLVM-LICENSE.txt`.
- binutils 2.44 `readelf`: `Copyright © 1991-2025 Free Software Foundation,
  Inc.` Permission is granted to copy, distribute and/or modify this document
  under the GNU Free Documentation License, Version 1.3 or any later version,
  with no Invariant Sections, no Front-Cover Texts, and no Back-Cover Texts.
- GDB 16.3 documentation: `Copyright (c) 1988-2024 Free Software Foundation,
  Inc.` Permission is granted to copy, distribute and/or modify this document
  under the GNU Free Documentation License, Version 1.3 or any later version;
  with the Invariant Sections being "Free Software" and "Free Software Needs
  Free Documentation", with the Front-Cover Texts being "A GNU Manual", and
  with the Back-Cover Texts as in (a) below.
  (a) The FSF's Back-Cover Text is: "You are free to copy and modify this GNU
  Manual. Buying copies from GNU Press supports the FSF in developing GNU and
  promoting software freedom."
- Arm AArch64 ABI 2024Q3:
  `Copyright © 2010, 2013, 2018, 2020-2024, Arm Limited and its affiliates. All
  rights reserved.` (aadwarf64);
  `Copyright © 2011, 2013, 2018-2024, Arm Limited and its affiliates. All
  rights reserved.` (aaelf64);
  `Copyright © 2011, 2013, 2018, 2020-2024, Arm Limited and its affiliates. All
  rights reserved.` (aapcs64). Licensed under CC-BY-SA-4.0 with the additional
  patent license reproduced in `CC-BY-SA-4.0-Arm-Patent-License.txt`. The Arm
  trademark notice states that Arm and the Arm trademarks are registered
  trademarks or trademarks of Arm Limited (or its affiliates).

### man-pages 6.9.1

| page | SPDX tag | copyright lines |
| --- | --- | --- |
| `ptrace.2` | `GPL-2.0-or-later` | `Copyright (c) 1993 Michael Haardt <michael@moria.de>`; changes `Copyright (C) 1999 Mike Coleman (mkc@acm.org)` |
| `wait.2` | `Linux-man-pages-copyleft` | `Copyright (c) 1993 by Thomas Koenig <ig25@rz.uni-karlsruhe.de>`; `Copyright (c) 2004 by Michael Kerrisk <mtk.manpages@gmail.com>` |
| `fork.2` | `Linux-man-pages-copyleft` | `Copyright (C) 2006 Michael Kerrisk <mtk.manpages@gmail.com>`; fragments from an earlier (1992) page by Drew Eckhardt |
| `execve.2` | `Linux-man-pages-copyleft` | `Copyright (c) 1992 Drew Eckhardt (drew@cs.colorado.edu), March 28, 1992`; `Copyright (c) 2006 Michael Kerrisk <mtk.manpages@gmail.com>` |
| `exec.3` | `BSD-4-Clause-UC` | `Copyright (c) 1991 The Regents of the University of California. All rights reserved.` |
| `personality.2` | `Linux-man-pages-copyleft` | `Copyright (C) 1995, Thomas K. Dyas <tdyas@eden.rutgers.edu>`; `Copyright (C) 2016, Michael Kerrisk <mtk.manpages@gmail.com>` |
| `core.5` | `Linux-man-pages-copyleft` | `Copyright (c) 2006, 2008 by Michael Kerrisk <mtk.manpages@gmail.com>` |
| `elf.5` | `PERMISSIVE_MISC` | `Copyright (c) 1999 Jeroen Ruigrok van der Werven. All rights reserved.` |

### Documents with no license text found

The following documents are bundled as reference material, but no license text
could be obtained from the pinned source. No license text is fabricated for
them, and this notice does not assert a license:

- **Itanium C++ ABI** (`environment/docs/itanium-cxx-abi/abi.html`). The file
  contains no copyright or license statement, and the upstream repository at
  the pinned revision has no `LICENSE`, `LICENSE.txt` or `COPYING` file at its
  root (all three paths return 404).
- **gABI** (`environment/docs/gabi/*.html`). The chapters carry only copyright
  notices: `© 1997, 1998, 1999, 2000, 2001 The Santa Cruz Operation, Inc. All
  rights reserved.`; `© 2002 Caldera International. All rights reserved.`;
  `© 2003-2010 The SCO Group. All rights reserved.`; `© 2011-2014 Xinuos Inc.
  All rights reserved.` No permission or license grant was found in the
  documents.
- **x86-64 psABI v1.0** (`environment/docs/x86-64-psabi-1.0/`). Neither the
  PDF nor its text conversion contains a copyright or license statement, and
  the pinned `x86-psABIs/x86-64-ABI` repository has no root `LICENSE` or
  `COPYING` file.

## Modifications made by the benchmark

- DWARF 2/3/4/5 specification text is stored as plain text in
  `steps/*/workdir/spec/` for offline reading. The conversion from the
  published PDFs preserves the text but replaces some typographic characters
  (for example, the DWARF 5 copyright page renders © as `c`); no substantive
  text or license notice was edited.
- Vendor documentation is stored as downloaded HTML (LLVM, binutils), as
  `gdb.info`/man-page text extracted from the tools' own documentation
  (GDB), as `pdftotext` conversions alongside the PDFs (Arm ABI, x86-64 psABI),
  or as groff source plus rendered text (man-pages).
- The GDB `remote-protocol.txt` and `target-descriptions.txt` are appendices
  extracted from `gdb.info`; they do not carry the manual's copyright page.
  The `gdbserver.1`/`gdbserver.1.txt` pair bundled beside them carries the full
  GDB copyright and GFDL notice quoted above. The invariant sections named in
  that notice are not part of the bundled excerpts.
- No document license notice was removed or altered.
- The Java toolkit, its tests and its fixtures are benchmark-authored. Test
  fixtures are compiled from benchmark-authored C sources with the system
  compiler; no compiler runtime is redistributed in the task tree.

## Third-party dependencies bundled in the task

This task bundles documentation only; it does not vendor or link any third-party
library. The documents and their licenses are listed above. The Java runtime
used to run the toolkit is supplied by the task image, not vendored in the task
tree.

## Required notices

The per-document copyright lines and license notices are quoted in the
"Copyright and license notices per document" section above. Trademarks named in
the documents (DWARF, LLVM, GNU, GDB, binutils, Arm, Itanium, Intel, AMD) remain
the property of their respective owners. This task claims no endorsement by any
of them.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
