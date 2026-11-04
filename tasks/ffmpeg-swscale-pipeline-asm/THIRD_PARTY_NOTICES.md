# Third-Party Notices — ffmpeg-swscale-pipeline-asm

Benchmark task that optimizes FFmpeg's `libswscale` scale-and-convert pipeline
one stage at a time, in dataflow order, across six cumulative steps.

## Upstream project

| | |
|---|---|
| Project | FFmpeg |
| URL | https://github.com/FFmpeg/FFmpeg |
| Pin | tag `n9.0.2`, commit `946fcce07b6dcd0331c8cc609192aeff5e1924f8` |
| Bundled/derived | The environment image clones the pinned FFmpeg tree and removes the six ladder stages. Each step's `solution/oracle.patch` is derived from the upstream FFmpeg sources at the pin and restores the corresponding `libswscale/x86` (and `libavfilter/x86/colorspacedsp.asm`) implementations. The measurement harness, strip tool and instructions are task-authored. |

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| LGPL-2.1-or-later | `LGPL-2.1-or-later.txt` | FFmpeg `n9.0.2` — the `libswscale` and `libavfilter` sources restored by the solution patches (exact upstream `COPYING.LGPLv2.1`; FFmpeg source headers state "either version 2.1 of the License, or (at your option) any later version") |

The pinned FFmpeg tree includes some files under other terms. FFmpeg's own
`LICENSE.md` states that most files are LGPL-2.1-or-later, that optional parts
are GPL-2.0-or-later when `--enable-gpl` is used, and that a handful of files
carry MIT/X11/BSD, libjpeg or expat terms. The files this task removes and
restores are the `libswscale` and `libavfilter` implementations, which are
LGPL-2.1-or-later. FFmpeg's `tests/checkasm/*` files are listed by `LICENSE.md`
as GPL-2.0-or-later; the verifier image builds them as a test harness, and no
checkasm source is shipped in this task tree.

## Modifications made by the benchmark

- The environment image starts from the pinned FFmpeg checkout and removes six
  stages (`in`, `rgb`, `ops`, `hs`, `out`, `yuv2rgb`) from `libswscale/x86`,
  from the other architecture implementations of the same stages, and from
  `libavfilter/x86/colorspacedsp.asm`. The portable C implementation is never
  touched, and the dispatch entry points remain.
- Each step's `solution/oracle.patch` restores the upstream files for that
  step; the added file contents are unmodified upstream FFmpeg sources.
- `environment/vendor/strip.py`, `PIN`, the measurement tools and the
  instructions are task-authored.

## Third-party dependencies bundled in the task

None. The task tree carries no third-party source beyond the FFmpeg-derived
patch content described above. The container image installs `gcc`, `nasm`,
`binutils` and other build tools from Debian; these are build dependencies and
are not redistributed in the task tree.

## Required notices

- FFmpeg is licensed under the GNU Lesser General Public License, version 2.1
  or later; the exact upstream `COPYING.LGPLv2.1` is reproduced in
  `LICENSES/LGPL-2.1-or-later.txt`.
- Individual FFmpeg source files carry their own copyright lines, which are
  retained in the restored files. For example, `libswscale/swscale.c` carries
  `Copyright (C) 2001-2011 Michael Niedermayer <michaelni@gmx.at>`, and
  `libswscale/x86/input.asm` carries `Copyright (c) 2012 Ronald S. Bultje
  <rsbultje@gmail.com>`.
- No upstream component in this task ships an Apache `NOTICE` file, so no
  additional Apache NOTICE text applies.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
