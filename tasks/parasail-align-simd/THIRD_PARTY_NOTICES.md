# Third-Party Notices: parasail-align-simd

## 1. Task

Seven-step task that vectorizes parasail's sequence-alignment families one
family at a time, graded by measured per-entry-point speedup against
bit-identical alignment output.

## 2. Upstream project and pin

| Upstream | URL | Pin | Bundled or derived in this task |
|---|---|---|---|
| parasail | https://github.com/jeffdaily/parasail | commit `fb985ee4f2302c72c87d7d56443712b2d920b106` | The agent and verifier images clone this commit at image-build time. The task tree does not store the upstream tree; it stores `environment/vendor/PIN`, the `strip.py` workspace transformer, the `bench_stage.c` measurement tool, and seven `steps/*/solution/oracle.patch` files whose added lines are the upstream vectorized implementations for that step's family. |

The upstream source is not copied into the repository; the Dockerfiles fetch it
from GitHub at the pinned commit and verify `git rev-parse HEAD` before use.

## 3. License map

| SPDX ID | File under `LICENSES/` | Component / version covered |
|---|---|---|
| BSD-3-Clause (with citation condition) | `BSD-3-Clause.txt` | parasail at `fb985ee`; exact upstream `LICENSE.md`, Copyright (c) 2015 Battelle Memorial Institute. Covers the upstream tree and the `oracle.patch` files derived from it. |
| — (disclaimer) | `DISCLAIMER.txt` | parasail's exact upstream `DISCLAIMER`: U.S. Government / Department of Energy / Battelle / Pacific Northwest National Laboratory disclaimer and contract statement. |
| MIT | `MIT-simde.txt` | SIMDe, vendored at `contrib/simde/` in the upstream tree; exact upstream `COPYING`, Copyright (c) 2017 Evan Nemerson. |
| MIT | `MIT-sais-lite.txt` | sais-lite-lcp, vendored at `contrib/sais-lite-lcp/`; exact upstream `COPYING`, Copyright (c) 2008-2010 Yuta Mori. |
| MIT | `MIT-kseq.txt` | kseq.h, `contrib/kseq.h`; exact license block from the file header, Copyright (c) 2008, 2009, 2011 Attractive Chaos. |
| ISC AND BSD-2-Clause | `ISC-and-BSD-2-Clause-wingetopt.txt` | wingetopt, `contrib/wingetopt/`; exact license blocks from its `README.md`, Copyright (c) 2002 Todd C. Miller and Copyright (c) 2000 The NetBSD Foundation. |
| CC-BY-3.0 | `CC-BY-3.0.txt` | `contrib/getMemorySize.c`, which declares the Creative Commons Attribution 3.0 Unported License in its header. |
| — (custom notice) | none | `contrib/cpuid.c` (Alexander J. Yee) has a custom permissive notice, quoted in section 6. `contrib/cpuid_gen4.c` reproduces code from an Intel article and states no license. `contrib/PW_sequence_alignment_2015.pdf` is a paper with no stated license. |

`contrib/ssw/` is a third-party SIMD Smith-Waterman library inside the upstream
tree. The benchmark's `strip.py` deletes it from every workspace and verifier
base; it is not bundled in the derived tree and no ssw license file is included.

## 4. Benchmark modifications

`environment/vendor/strip.py` produces every step's base tree from the pinned
commit:

- Deletes the generated vectorized implementation files for the families named
  by the step's keys, across all strategies, five ISAs and four solution widths.
- Removes those files from `Makefile.am`, `CMakeLists.txt` and
  `src/meson.build`; removes their symbols from `function_lookup.h` and the
  generated `tests/func_verify*.h` groups; and removes the `#if HAVE_<ISA>`
  blocks in `src/satcheck.c` that call them.
- Rewrites each family's dispatchers so the fall-through points at that
  family's own non-vectorized reference instead of upstream's generic
  `parasail_sw` fall-through; the profile dispatchers return `NULL`.
- Deletes `contrib/ssw/`, its test drivers, `tests/test_12.c` and
  `tests/test_64.c`, and the corresponding build entries.

The seven `steps/*/solution/oracle.patch` files restore the removed upstream
implementations for that step's family. They are the difference between two
trees built from the same pin.

## 5. Bundled third-party dependencies

The upstream tree built by `environment/Dockerfile` includes these vendored
components; the paths are relative to the cloned parasail tree.

| Name | Version | License | Bundled path |
|---|---|---|---|
| SIMDe | vendored at the pin | MIT | `contrib/simde/` |
| sais-lite-lcp | vendored at the pin | MIT | `contrib/sais-lite-lcp/` |
| kseq.h | vendored at the pin | MIT | `contrib/kseq.h` |
| wingetopt | vendored at the pin | ISC AND BSD-2-Clause | `contrib/wingetopt/` |
| getMemorySize.c | vendored at the pin | CC-BY-3.0 | `contrib/getMemorySize.c` |
| cpuid.c | vendored at the pin | Custom permissive notice (section 6) | `contrib/cpuid.c` |
| cpuid_gen4.c | vendored at the pin | No license stated | `contrib/cpuid_gen4.c` |
| PW sequence-alignment paper | 2015 | No license stated | `contrib/PW_sequence_alignment_2015.pdf` |
| ssw | vendored upstream, deleted by `strip.py` | Not bundled | `contrib/ssw/` (removed) |

## 6. Required notices

parasail's license requires a citation for redistributions and publications
based on work using the software:

> Daily, Jeff. (2016). Parasail: SIMD C library for global, semi-global, and
> local pairwise sequence alignments. *BMC Bioinformatics*, 17(1), 1-11.
> doi:10.1186/s12859-016-0930-z

Copyright notices:

- parasail: "Copyright (c) 2015, Battelle Memorial Institute."
- SIMDe: "Copyright (c) 2017 Evan Nemerson <evan@nemerson.com>"
- sais-lite: "Copyright (c) 2008-2010 Yuta Mori All Rights Reserved."
- kseq.h: "Copyright (c) 2008, 2009, 2011 Attractive Chaos <attractor@live.co.uk>"
- wingetopt: "Copyright (c) 2002 Todd C. Miller" and "Copyright (c) 2000 The
  NetBSD Foundation, Inc. All rights reserved."
- getMemorySize.c: "Author: David Robert Nadeau; License: Creative Commons
  Attribution 3.0 Unported License."
- cpuid.c exact notice:

  > Redistribution of this program in both source or binary, regardless of
  > form, with or without modification is permitted as long as the following
  > conditions are met: 1. This copyright notice is maintained either inline in
  > the source or distributed with the binary. 2. A list of all contributing
  > authors along with their contributions is included either inline in the
  > source or distributed with the binary. 3. The following disclaimer is
  > maintained either inline in the source or distributed with the binary.
  >
  > Disclaimer: This software is provided "as is", without any guarantee made
  > to its suitability or fitness for any particular use. It may contain bugs so
  > use of this program is at your own risk. I take no responsibility for any
  > damage that may unintentionally be caused through its use.

The U.S. Government disclaimer in `LICENSES/DISCLAIMER.txt` is reproduced from
upstream and applies to the extent stated there.

## 7. Closing note

Task-authored benchmark content is licensed separately by the benchmark maintainers.
