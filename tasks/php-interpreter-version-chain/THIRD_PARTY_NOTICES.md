# Third-Party Notices: php-interpreter-version-chain

## 1. Task

Carry a from-scratch PHP interpreter written in C through eleven releases of the
PHP release train, 5.6 to 8.4, graded with php-src's own `.phpt` tests at each
release tag.

## 2. Upstream projects and pins

| Upstream | URL | Pins | Bundled or derived in this task |
|---|---|---|---|
| php-src | https://github.com/php/php-src | tags `php-5.6.40`, `php-7.0.33`, `php-7.1.33`, `php-7.2.34`, `php-7.3.33`, `php-7.4.33`, `php-8.0.30`, `php-8.1.31`, `php-8.2.27`, `php-8.3.16`, `php-8.4.4` | Each step bundles that tag's `.phpt` tests from `Zend/tests/` and `ext/{ctype,json,mbstring,standard}/tests/` under `steps/<step>/workdir/tests/phpt/` and `steps/<step>/tests/pool/`, plus a `run-tests-ini.txt` derived from that tag's `run-tests.php`. No php-src implementation source is bundled; the interpreter under `steps/*/solution/src/` is authored for the benchmark. |
| php/doc-en | https://github.com/php/doc-en | commit `a330e2e2d30b14662ead3c060b31101934097bb2` | `environment/docs/php-manual/` carries a subset of the English manual DocBook source: the language reference, `features`, the appendices (including the 5.6-to-8.4 migration guides), and the function references for the extensions exercised by the tests. |

The `.phpt` count is 21,591 files across all steps, including the copies kept in
each step's `tests/pool/`. Each graded workdir carries 1,500-1,509 `.phpt`
files; each `tests/pool/` carries another 280-615.

## 3. License map

| SPDX ID | File under `LICENSES/` | Component / version covered | In-tree evidence |
|---|---|---|---|
| PHP-3.01 | `PHP-3.01-5.6.40.txt` | php-src `php-5.6.40`, Copyright (c) 1999-2016 The PHP Group | `steps/01-php-5-6/workdir/tests/phpt/PROVENANCE.txt` names the tag; the licence is the tag's root `LICENSE` |
| PHP-3.01 | `PHP-3.01-7.0.33.txt` | php-src `php-7.0.33`, Copyright (c) 1999-2017 The PHP Group | `steps/02-php-7-0/workdir/tests/phpt/PROVENANCE.txt` |
| PHP-3.01 | `PHP-3.01-7.1.33-7.3.33.txt` | php-src `php-7.1.33`, `php-7.2.34`, `php-7.3.33`, Copyright (c) 1999-2018 The PHP Group | `steps/03-php-7-1`, `steps/04-php-7-2`, `steps/05-php-7-3` `PROVENANCE.txt` |
| PHP-3.01 | `PHP-3.01-7.4.33-8.1.31.txt` | php-src `php-7.4.33`, `php-8.0.30`, `php-8.1.31`, Copyright (c) 1999-2021 The PHP Group | `steps/06-php-7-4`, `steps/07-php-8-0`, `steps/08-php-8-1` `PROVENANCE.txt` |
| PHP-3.01 | `PHP-3.01.txt` | php-src `php-8.2.27`, `php-8.3.16`, `php-8.4.4`, Copyright (c) 1999-2024 The PHP Group | `steps/09-php-8-2`, `steps/10-php-8-3`, `steps/11-php-8-4` `PROVENANCE.txt` |
| Zend-Engine-2.00 | `Zend-Engine-2.00.txt` | The `Zend/tests/` subtree of each tag; Zend Engine License 2.00, Copyright (c) 1999-2006 Zend Technologies Ltd. | `steps/*/workdir/tests/phpt/Zend/tests/`, `steps/*/tests/pool/Zend/tests/` |
| CC-BY-3.0 | `CC-BY-3.0.txt` | php/doc-en manual subset; Copyright © 1997-[current year] by the PHP Documentation Group | `environment/docs/php-manual/bookinfo.xml` legal notice and `environment/docs/php-manual/appendices/license.xml`; `environment/docs-provenance.txt` records the commit and archive SHA-256 |

The PHP licence text is identical across the five variants except for the
copyright year in the header. The 5.6.40 and 7.0.33 files retain the trailing
spaces present in those tags; the later files do not.

## 4. Benchmark modifications

- Each step's test directory is a curated subset of the tag's `Zend/tests/` and
  `ext/*/tests/` trees, reorganised as `tests/phpt/Zend/tests/` and
  `tests/phpt/ext/<extension>/tests/`. The `.phpt` file contents are taken
  verbatim from the named tag; only the directory layout and selection are the
  benchmark's.
- `run-tests-ini.txt` is derived from that tag's `run-tests.php`
  `$ini_overwrites` list with the opcache entries removed, because the
  benchmark interpreter does not implement opcache.
- A second copy of selected tests is kept under each step's `tests/pool/` for
  grading and re-scoring.
- The interpreter, its build files and the step patches under
  `steps/*/solution/` are authored for the benchmark. They are not php-src
  code.
- The manual under `environment/docs/php-manual/` is a subset of the pinned
  `php/doc-en` archive. Files are copied without textual changes, and
  `MANIFEST.sha256` records the shipped subset.

## 5. Bundled third-party dependencies

| Name | Version | License | Bundled path |
|---|---|---|---|
| php-src test suite | tags listed in section 2 | PHP-3.01; Zend-Engine-2.00 for `Zend/tests/` | `steps/*/workdir/tests/phpt/`, `steps/*/tests/pool/` |
| PHP manual (php/doc-en) | commit `a330e2e` | CC-BY-3.0 | `environment/docs/php-manual/` |

No PHP binary, php-src source tree, Zend Engine implementation, TSRM source or
extension library source is bundled. The base image is Debian bookworm-slim
with a C toolchain and Node.js 24.22.0; those are system packages under their
own distribution licences and are not redistributed in the task tree.

## 6. Required notices

- PHP 5.6.40: "Copyright (c) 1999 - 2016 The PHP Group. All rights reserved."
- PHP 7.0.33: "Copyright (c) 1999 - 2017 The PHP Group. All rights reserved."
- PHP 7.1.33, 7.2.34 and 7.3.33: "Copyright (c) 1999 - 2018 The PHP Group. All
  rights reserved."
- PHP 7.4.33, 8.0.30 and 8.1.31: "Copyright (c) 1999 - 2021 The PHP Group. All
  rights reserved."
- PHP 8.2.27, 8.3.16 and 8.4.4: "Copyright (c) 1999 - 2024 The PHP Group. All
  rights reserved."
- Zend Engine: "Copyright (c) 1999-2006 Zend Technologies Ltd. All rights
  reserved." The Zend Engine License requires the acknowledgment: "This
  product includes the Zend Engine, freely available at
  http://www.zend.com". It also reserves the names "Zend" and "Zend Engine";
  they must not be used to endorse or promote products derived from the covered
  code without prior written permission from Zend Technologies Ltd.
- PHP manual: "Copyright © 1997 - [current year] by the PHP Documentation Group.
  This material may be distributed only subject to the terms and conditions set
  forth in the Creative Commons Attribution 3.0 License or later." The
  `bookinfo.xml` legal notice uses the DocBook `dbtimestamp` element for the
  current year. The manual's `appendices/license.xml` carries the licence text
  in DocBook form. `LICENSES/CC-BY-3.0.txt` is the canonical Creative Commons
  Attribution 3.0 Unported text.

## 7. Closing note

Task-authored benchmark content is licensed separately by the benchmark maintainers.
