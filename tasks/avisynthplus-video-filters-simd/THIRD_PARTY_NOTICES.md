# Third-Party Notices — avisynthplus-video-filters-simd

A seven-step benchmark ladder in which a candidate restores the vectorized
Intel implementations of AviSynth+ filter families (overlay masked blend,
focus/blur/sharpen, layer, turn/rotate, planeswap/greyscale, merge/average, and
limiter/frame-stats) in a tree whose `*/intel/*` and `*/aarch64/*` sources have
been removed.

## Upstream project

| | |
|---|---|
| Project | AviSynth+ |
| Repository | https://github.com/AviSynth/AviSynthPlus |
| Pin | commit `5c82777b374bdef16e13007a11e77d735ac1e4eb` |
| Pinned tarball | `environment/vendor/pin.tar.gz`, SHA-256 `375a64faa587a516476a867211eef23b8560fb310c7f7ac1cdc11a3c907b2da8` |
| Upstream license | GPL-2.0-or-later, with the `avisynth.h` linking exception; LGPL-2.1-or-later components; OFL-1.1 font; CC-BY-SA-3.0 documentation |

### Bundled and derived material

* `environment/vendor/pin.tar.gz` — the full upstream source tree at the pin,
  generated with `git archive` and committed to this task repository. It
  contains upstream's own license files: `distrib/gpl.txt`,
  `distrib/lgpl_for_used_libs.txt`, `distrib/OFL.TXT`, and the documentation
  with its license statement in
  `distrib/docs/english/source/avisynthdoc/license.rst`.
* `steps/*/tests/vendor/pin.tar.gz` — the same pinned tarball, copied into each
  verifier image.
* `steps/*/tests/vendor/base-lib.stepNN.tar.gz` — `libavisynth.so.3.7.5` built
  from the stripped tree. These are binaries of GPL-2.0-or-later code; the
  corresponding source is `pin.tar.gz` together with `environment/vendor/mklevel.py`.
* `steps/*/solution/oracle.patch` — seven patches derived from upstream
  `*/intel/*` sources at the pin. Files carried over from upstream retain their
  upstream copyright and license headers.
* `environment/vendor/avsbench.c`, `environment/vendor/mklevel.py`, and the
  per-step test and build scripts — benchmark-authored tooling.

## Modifications made by the benchmark

`environment/vendor/mklevel.py` builds each milestone's tree from the unmodified
pinned tarball by:

* removing every `*/intel/*` and `*/aarch64/*` source except the ladder rows
  selected for that milestone;
* evaluating `INTEL_INTRINSICS` / `NEON_INTRINSICS` preprocessor conditionals to
  their portable C++ arm;
* deleting three upstream changelog/readme files that describe the removed
  kernels, and dropping whole-line comments that name them.

The `pin.tar.gz` archive itself is byte-for-byte upstream. The `base-lib` shared
libraries are compiled from the modified tree.

## Licenses

| SPDX ID | File under `LICENSES/` | Applies to |
|---|---|---|
| GPL-2.0-or-later | `GPL-2.0-or-later.txt` | AviSynth+ main license. Verbatim upstream `distrib/gpl.txt` (GPL version 2 text); upstream source headers grant use under "version 2 of the License, or (at your option) any later version". |
| AviSynth-linking-exception | `AviSynth-linking-exception.txt` | The linking exception in `avs_core/include/avisynth.h`, extracted verbatim (comment markers removed). |
| LGPL-2.1-or-later | `LGPL-2.1-or-later.txt` | LGPL components in the tree, principally the Shibatch SuperEQ/SSRC code in `plugins/Shibatch/`. The complete canonical LGPL-2.1 text is reproduced here; AviSynth+ ships the same LGPL terms at `distrib/lgpl_for_used_libs.txt`, whose copy omits the version header and preamble. |
| OFL-1.1 | `OFL-1.1.txt` | Terminus Font 4.48, embedded in `avs_core/core/fonts/fixedfonts.cpp`. Verbatim upstream `distrib/OFL.TXT`, including the font copyright line. |
| CC-BY-SA-3.0 | `CC-BY-SA-3.0.txt` | AviSynth documentation under `distrib/docs/` (upstream statement in `distrib/docs/english/source/avisynthdoc/license.rst`). |

## Third-party dependencies bundled in `pin.tar.gz`

| Component | In-tree location | License | Copyright / notice |
|---|---|---|---|
| VirtualDub-derived code (frame client, OpenDML, part of the subtitling code, CPU extensions, VirtualDub plugin interface) | `avs_core/`, `plugins/VDubFilter/`, `plugins/VFAPIFilter/` | GPL-2.0-or-later, with Avery Lee's special permission to distribute the VirtualDub-derived elements under AviSynth's GPL-plus-exception | Copyright (C) 1999-2001 Avery Lee |
| bbMPEG-derived DirectShowSource filter | `plugins/DirectShowSource/` | Freeware, "Freely distributable" per upstream `license.rst` | Brent Beyeler, 1999-2000 |
| Avisynth C interface | `avs_core/include/avisynth_c.h`, `avs_core/core/avisynth_c.cpp` | GPL-2.0-or-later with the `avisynth_c.h` linking exception | Copyright (C) 2003-2004 Kevin Atkinson |
| Shibatch SuperEQ and SSRC | `plugins/Shibatch/` | LGPL-2.1-or-later (the FFT part is under the Ooura terms below) | Copyright (C) 2001-2003 Naoki Shibata; changes copyright (C) 2001-2003 Peter Pawlowski and (C) 2003 Klaus Post |
| PFC class library (portion of the Foobar2000 0.7 SDK) | `plugins/Shibatch/mem_block.cpp`, `plugins/Shibatch/mem_block.h` | BSD-3-Clause-like terms (text below) | Copyright (C) 2001-2003 Peter Pawlowski |
| General Purpose FFT package (Ooura) | `plugins/Shibatch/fft.h` | Permissive terms (text below) | Copyright (C) 1996-2001 Takuya OOURA |
| Terminus Font 4.48 | `avs_core/core/fonts/fixedfonts.cpp` | OFL-1.1 (`LICENSES/OFL-1.1.txt`) | Copyright (c) 2019 Dimitar Toshkov Zhekov |

SoundTouch (TimeStretch), ImageLib/DevIL (ImageSeq) and SoftWire are documented
by upstream as optional external dependencies; their source is not bundled in
this pinned tree. SoftWire is documented upstream but no SoftWire source is
present at this pin.

## Required notices

AviSynth copyright and documentation attribution (upstream `license.rst`):

```
AviSynth v2.5 Copyright (C) 2002-2006  Ben Rudiak-Gould et al.

AviSynth documentation is Copyright (C) 2002-2007 AviSynth developers and
contributors.  Starting from 5 August 2007 the documentation is released
under the Creative Commons Attribution-ShareAlike 3.0 License (CC BY-SA 3.0).
```

PFC / Foobar2000 0.7 SDK terms, reproduced verbatim from upstream `license.rst`:

```
Copyright (C) 2001-2003, Peter Pawlowski
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met: Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
Neither the name of the author nor the names of its contributors may
be used to endorse or promote products derived from this software
without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

General Purpose FFT package terms, reproduced verbatim from upstream `license.rst`:

```
Copyright (C) 1996-2001 Takuya OOURA
(Email: ooura@kurims.kyoto-u.ac.jp or ooura@mmm.t.u-tokyo.ac.jp)

You may use, copy, modify and distribute this code for any purpose
(include commercial use) and without fee.
Please refer to this package when you modify this code.
```

Avisynth C interface exception, reproduced verbatim from upstream `license.rst`:

```
As a special exception, I give you permission to link to the
Avisynth C interface with independent modules that communicate with
the Avisynth C interface solely through the interfaces defined in
avisynth_c.h, regardless of the license terms of these independent
modules, and to copy and distribute the resulting combined work
under terms of your choice, provided that every copy of the
combined work is accompanied by a complete copy of the source code
of the Avisynth C interface and Avisynth itself (with the version
used to produce the combined work), being distributed under the
terms of the GNU General Public License plus this exception.  An
independent module is a module which is not derived from or based
on Avisynth C Interface, such as 3rd-party filters, import and
export plugins, or graphical user interfaces.
```

VirtualDub-derived elements are used with the upstream notice: "Special
permission by Avery Lee to distribute existing (at November 2006)
VirtualDub-derived elements in Avisynth under the current Avisynth license,
which is GPL plus a special exemption."

---

Task-authored benchmark content is licensed separately by the benchmark maintainers.
