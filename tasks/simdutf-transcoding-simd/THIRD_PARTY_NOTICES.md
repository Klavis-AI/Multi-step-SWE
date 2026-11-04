# Third-party notices — simdutf-transcoding-simd

Optimize simdutf's Unicode transcoding surface one operation family at a time:
seven cumulative steps, each rewarded by the measured per-function speedup of
the family that step owns, gated on bit-exact output and an unchanged
error-position contract.

## Upstream project

| | |
|---|---|
| Project | simdutf |
| URL | https://github.com/simdutf/simdutf |
| Pin | tag `v9.2.0`, commit `8abc1d7a466bc882c2d72e1effd8661492db257c` |
| Bundled | Full upstream source tree cloned into `/app/simdutf` at image build time (`environment/Dockerfile`), minus `.git` |
| Derived | Seven `steps/*/solution/oracle.patch` files containing the optimized implementations restored by the reference solution |

The task's own workload, timer, corpus generator and harnesses are authored
benchmark content. Upstream's `tests/` suite is present unmodified and is used
as the documented correctness check.

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| Apache-2.0 OR MIT | `LICENSES/Apache-2.0.txt`, `LICENSES/MIT.txt` | simdutf itself (`LICENSE-APACHE`, `LICENSE-MIT`) |
| BSL-1.0 | `LICENSES/BSL-1.0.txt` | `benchmarks/competition/utf8lut/` (`LICENSE`) and `benchmarks/competition/utfcpp/` (`LICENSE`) |
| OSL-3.0 | `LICENSES/OSL-3.0.txt` | `benchmarks/competition/u8u16/` (`OSL3.0.txt`) |
| LGPL-2.1-only | `LICENSES/LGPL-2.1-only.txt` | `benchmarks/competition/utf8sse4/fromutf8-sse.cpp` (header grants LGPL version 2.1) |
| Apache-2.0 WITH LLVM-exception | `LICENSES/Apache-2.0.txt`, `LICENSES/LLVM-exception.txt` | `benchmarks/competition/llvm/ConvertUTF.{h,cpp}` |
| No license text in tree | — | `benchmarks/competition/hoehrmann/hoehrmann.h` — copyright notice only (see below) |
| All Rights Reserved (no license text in tree) | — | `benchmarks/competition/CppCon2018/` — see below |
| Benchmark-use-only (no license text in tree) | — | `benchmarks/competition/inoue2008/` — see below |

`benchmarks/competition/README.md` states that the code in that directory is
used solely for benchmarking, "under the respective licenses", and "is not part
of the simdutf library."

## What is bundled

The image clones the pinned upstream tree and then removes the optimized
implementations of the seven operation families the ladder covers
(`utf8_to_utf16_and_utf32`, `utf16_to_utf8`, `utf32_to_utf8`, `utf16_utf32`,
`latin1`, `base64`, `validate_misc`) together with the non-x86 back-ends, as
listed in `environment/vendor/strip.py`. The library remains complete and
correct through its scalar fallback.

Every `steps/*/solution/oracle.patch` is derived from the upstream
implementation at the same commit; the reference solution applies it to
restore one family's optimized code.

## Modifications made by the benchmark

- The SIMD implementations owned by each step's family are removed from the
  starting tree; the scalar reference under `include/simdutf/scalar/` is kept
  unchanged.
- Non-x86 back-ends are removed from the image tree.
- One upstream CMake POST_BUILD rule that creates an absolute symlink is
  rewritten to the relative target upstream's install rule uses
  (`environment/vendor/relativize-fastbase64-symlink.py`); the upstream
  `tools/CMakeLists.txt` rule itself is unchanged in the candidate tree.
- The seven `oracle.patch` files are benchmark-derived patches, not upstream
  files.
- A sample workload (`/app/workload`) and a corpus generator
  (`/app/tools/make_corpus.py`) are authored benchmark content, not upstream
  data.
- No upstream file outside `benchmarks/` is edited in the shipped tree.

## Third-party dependencies bundled in the task

The full upstream tree is present in the image, so the vendored benchmark
competition code ships with it even though the graded harness does not use it.

| Component | Version | License | Bundled path |
|---|---|---|---|
| simdutf | 9.2.0 (`8abc1d7`) | Apache-2.0 OR MIT | `/app/simdutf` (whole tree) |
| utf8lut | upstream at pin | BSL-1.0 | `benchmarks/competition/utf8lut/` |
| utfcpp | upstream at pin | BSL-1.0 | `benchmarks/competition/utfcpp/` |
| u8u16 (International Characters) | upstream at pin | OSL-3.0 | `benchmarks/competition/u8u16/` |
| utf8sse4 (Olivier Goffart, woboq) | upstream at pin | LGPL-2.1-only | `benchmarks/competition/utf8sse4/fromutf8-sse.cpp` |
| LLVM ConvertUTF | upstream at pin | Apache-2.0 WITH LLVM-exception | `benchmarks/competition/llvm/ConvertUTF.{h,cpp}` |
| hoehrmann UTF-8 DFA | upstream at pin | Copyright notice only; no license grant in file | `benchmarks/competition/hoehrmann/hoehrmann.h` |
| CppCon2018 UTF-8 conversion | upstream at pin | All Rights Reserved (see notice) | `benchmarks/competition/CppCon2018/utf_utils.{h,cpp}` |
| inoue2008 SIMD UTF-8 decoding | upstream at pin | Benchmark use within simdutf only | `benchmarks/competition/inoue2008/` |
| Wikipedia text excerpts | upstream at pin | Not stated in the tree | `benchmarks/dataset/wikipedia_mars/` |

## Required notices

simdutf:

> Copyright 2021 The simdutf authors

The MIT option in `LICENSE-MIT` carries that line; the Apache-2.0 option in
`LICENSE-APACHE` is the unmodified Apache License 2.0 text and has no NOTICE
file.

utf8lut and utfcpp are distributed under the Boost Software License 1.0; the
license text in `LICENSES/BSL-1.0.txt` is byte-identical to both upstream
`LICENSE` files.

u8u16:

> Copyright (C) 2007 International Characters Inc.
> Licensed to the public under the Open Software License version 3.0.
> Patents pending in Canada, the United States, and other countries.
> International Characters is a trademark of International Characters Inc.
> Commercial software licenses are also available.

utf8sse4:

> Copyright (C) 2012 Olivier Goffart <ogoffart@woboq.com>
> This file may be used under the terms of the GNU Lesser General Public
> License version 2.1 as published by the Free Software Foundation.

LLVM ConvertUTF: the file header states "Part of the LLVM Project, under the
Apache License v2.0 with LLVM Exceptions" and also reproduces the Unicode, Inc.
disclaimer and redistribution grant for the original 2001-2004 Unicode code.

hoehrmann: the header carries only

> Copyright (c) 2008-2010 Bjoern Hoehrmann <bjoern@hoehrmann.de>
> See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.

No license text is included in the file in the upstream tree. The MIT text in
`LICENSES/MIT.txt` is the simdutf project license, not a license grant from
Bjoern Hoehrmann.

CppCon2018: the header states "Copyright (c) 2018 Bob Steagall and KEWB
Computing, All Rights Reserved". No license grant is included in the upstream
file.

inoue2008: the header states the code is "for comparison purposes only", "Not
to be released as part of our public API", "private code", and "Do not use
this code for any other purpose than for benchmarking within simdutf." No
license grant is included in the upstream file.

The task is not affiliated with or endorsed by the simdutf project.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
