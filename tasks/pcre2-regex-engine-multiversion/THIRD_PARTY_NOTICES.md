# Third-Party Notices: pcre2-regex-engine-multiversion

## 1. Task

Thirteen milestones of the PCRE2 regular-expression engine, from the 10.20
recursive matcher to the 10.48 Unicode 17 release. Each step reproduces one
upstream release's observable behaviour through `pcre2test`.

## 2. Upstream project and pins

| Upstream | URL | Pins | Bundled or derived in this task |
|---|---|---|---|
| PCRE2 | https://github.com/PCRE2Project/pcre2 | tags `pcre2-10.20`, `pcre2-10.22`, `pcre2-10.23`, `pcre2-10.30`, `pcre2-10.32`, `pcre2-10.33`, `pcre2-10.34`, `pcre2-10.39`, `pcre2-10.42`, `pcre2-10.43`, `pcre2-10.45`, `pcre2-10.47`, `pcre2-10.48` | Each step's `workdir/pcre2-doc/` carries that release's manual pages and `LICENCE`/`LICENCE.md`; each `testdata/` carries that release's published test inputs and expected outputs. The step-1 `src/` scaffold carries upstream headers and generated tables (`pcre2.h.generic`, `pcre2.h.in`, `config.h.generic`, `pcre2_internal.h`, `pcre2_intmodedep.h`, `pcre2_ucp.h`, `pcre2posix.h`, `pcre2_chartables.c.dist`, `pcre2_ucd.c`, `pcre2test.c`, `pcre2_printint.c`). The engine implementation is authored for the benchmark; no upstream engine source is copied. |
| Unicode Character Database | https://www.unicode.org/versions/Unicode17.0.0/ | Unicode 17.0.0 | `unicode-data/17.0.0/` in the step workdirs: generated `pcre2_ucd.c`, `pcre2_ucp.h` and `pcre2_ucptables_inc.h`, plus a `VERSION` file. They are reference data at an inert path. |

The in-tree license files are the primary source for the texts under
`LICENSES/`; the versions that changed across releases are mapped below.

## 3. License map

| SPDX ID | File under `LICENSES/` | Release(s) covered | In-tree path |
|---|---|---|---|
| BSD-3-Clause | `BSD-3-Clause-10.20.txt` | 10.20 (2015 text) | `steps/01-recursive-engine/workdir/pcre2-doc/LICENCE` |
| BSD-3-Clause | `BSD-3-Clause-10.22.txt` | 10.22 (2016 text) | `steps/02-find-bracket-and-posix-nosub/workdir/pcre2-doc/LICENCE` |
| BSD-3-Clause | `BSD-3-Clause-10.23.txt` | 10.23 (2017 text) | `steps/03-compile-path-restructure/workdir/pcre2-doc/LICENCE` |
| BSD-3-Clause WITH PCRE2-exception | `BSD-3-Clause-WITH-PCRE2-exception-10.30.txt` | 10.30 (first text with the binary-library exemption) | `steps/04-heap-frame-matcher/workdir/pcre2-doc/LICENCE` |
| BSD-3-Clause WITH PCRE2-exception | `BSD-3-Clause-WITH-PCRE2-exception-10.32.txt` | 10.32 | `steps/05-dfa-derecursion/workdir/pcre2-doc/LICENCE` |
| BSD-3-Clause WITH PCRE2-exception | `BSD-3-Clause-WITH-PCRE2-exception-10.33-10.34.txt` | 10.33 and 10.34 (identical text) | `steps/06-script-runs/workdir/pcre2-doc/LICENCE`; `steps/07-invalid-utf-and-nonatomic-lookaround/workdir/pcre2-doc/LICENCE` |
| BSD-3-Clause WITH PCRE2-exception | `BSD-3-Clause-WITH-PCRE2-exception-10.39.txt` | 10.39 | `steps/08-backslash-k-restriction/workdir/pcre2-doc/LICENCE` |
| BSD-3-Clause WITH PCRE2-exception | `BSD-3-Clause-WITH-PCRE2-exception-10.42.txt` | 10.42 | `steps/09-script-properties/workdir/pcre2-doc/LICENCE` |
| BSD-3-Clause WITH PCRE2-exception | `BSD-3-Clause-WITH-PCRE2-exception-10.43.txt` | 10.43 | `steps/10-variable-lookbehind/workdir/pcre2-doc/LICENCE` |
| BSD-3-Clause WITH PCRE2-exception | `BSD-3-Clause-WITH-PCRE2-exception-10.45.txt` | 10.45 (Markdown licence) | `steps/11-optimize-control-and-extended-classes/workdir/pcre2-doc/LICENCE.md` |
| BSD-3-Clause WITH PCRE2-exception | `BSD-3-Clause-WITH-PCRE2-exception-10.47-10.48.txt` | 10.47 and 10.48 (identical text) | `steps/12-next-match-and-scs-fix/workdir/pcre2-doc/LICENCE.md`; `steps/13-unicode-17-and-harness-rewrite/workdir/pcre2-doc/LICENCE.md` |
| Public domain | `Public-Domain.txt` | `testdata/` in every release | Statement from the PCRE2 licence: "The data in the testdata directory is not copyrighted and is in the public domain." |
| Unicode-3.0 | `Unicode-3.0.txt` | Unicode 17.0.0 data used for the generated tables | Not stored in-tree; exact `https://www.unicode.org/license.txt` |

The 10.20-10.23 licences do not contain the binary-library exemption; from
10.30 onward the licence text carries it. `deps/sljit` has its own licence, but
sljit is not bundled in this task tree.

## 4. Benchmark modifications

- Each step's workspace is overlaid with that release's `pcre2-doc/` manual
  pages, `PROFILE.md`, `NEWS`/`ChangeLog.transition`, `testdata/` and
  `TEST_INTERFACE.md`; earlier overlays are replaced by later ones.
- The step-1 scaffold contains only the upstream headers, generated data and
  test driver listed in `src/SCAFFOLD_MANIFEST.txt`; the engine, compiler,
  matchers, substitution, serialization and POSIX wrapper are authored for the
  benchmark and are not upstream source.
- `unicode-data/17.0.0/` is shipped as inert reference data; building the
  tables from the UCD is out of scope.
- The test corpora contain upstream `testinput*`/`testoutput*` files, copied
  into per-step `tests/corpus/` directories, plus benchmark-generated gate and
  capture cases derived from them.
- The solution `src.tar.gz` in each step is a benchmark-authored implementation.

## 5. Bundled third-party dependencies

| Name | Version | License | Bundled path |
|---|---|---|---|
| PCRE2 release documentation, test data and scaffold headers/tables | 10.20-10.48 | BSD-3-Clause WITH PCRE2-exception (BSD-3-Clause for 10.20-10.23) | `steps/*/workdir/pcre2-doc/`, `steps/*/workdir/testdata/`, step-1 `src/` |
| Unicode Character Database generated tables | Unicode 17.0.0 | Unicode-3.0 | `steps/*/workdir/unicode-data/17.0.0/` |
| PCRE2 `testdata` corpus | 10.20-10.48 | Public domain | `steps/*/workdir/testdata/`, `steps/*/tests/corpus/` |

No sljit, JIT compiler or other third-party library is bundled.

## 6. Required notices

- PCRE2 10.20-10.43: the licence carries a single copyright line, "Copyright
  (c) 1997-<release year> University of Cambridge", with the ending year 2015,
  2016, 2017, 2018, 2019, 2021, 2022 and 2024 respectively as reproduced in
  each `LICENSES/` file.
- PCRE2 10.45-10.48: "Copyright (c) 1997-2007 University of Cambridge";
  "Copyright (c) 2007-2024 Philip Hazel. All rights reserved.";
  "Copyright (c) 2010-2024 Zoltan Herczeg. All rights reserved." (JIT support);
  and "Copyright (c) 2009-2024 Zoltan Herczeg. All rights reserved."
  (stack-less JIT compiler).
- The PCRE2 licence states that all other contributions retain copyright with
  each original contributor or their employer.
- Unicode data: "Copyright © 1991-2026 Unicode, Inc." from the Unicode License
  V3; Unicode and the Unicode Logo are registered trademarks of Unicode, Inc.
- Public-domain statement for test data: "The data in the testdata directory is
  not copyrighted and is in the public domain."

## 7. Closing note

Task-authored benchmark content is licensed separately by the benchmark maintainers.
