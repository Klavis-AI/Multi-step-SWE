# Third-party notices — python-package-resolver-multiversion

A fifteen-step benchmark in which a candidate builds a pip-compatible Python
package resolver in Rust, starting from the published `uv` 0.1.0 source tree
and carrying it through upstream releases up to 0.12.17.

## Upstream projects

| | |
|---|---|
| Project | uv (Astral) |
| URL | https://github.com/astral-sh/uv |
| Releases pinned | 0.1.0, 0.1.24, 0.2.0, 0.3.0, 0.4.20, 0.5.0, 0.5.20, 0.6.0, 0.7.0, 0.8.0, 0.9.0, 0.10.0, 0.11.0, 0.12.0, 0.12.17 |
| Bundled material | Full source tree at tag 0.1.0 (`steps/01-pip-compatible-core/solution/uv-0.1.0-tree.tar.gz`, 507 entries); release-to-release source patches for every later pin; upstream licence texts |
| Project | Python Enhancement Proposals (python/peps) |
| URL | https://github.com/python/peps |
| Pin | commit `8766f78fee7b2bd08bce0537fcb2c9b103b10d71` (2026-09-18) |
| Bundled material | 20 `pep-NNNN.rst` documents under `steps/*/workdir/SPECS/specs/` |
| Project | Python Packaging User Guide / PyPA specifications (pypa/packaging.python.org) |
| URL | https://github.com/pypa/packaging.python.org |
| Pin | commit `457e74f03e630c7d4414bb39e8e77d4b61df6ee9` (2026-09-09) |
| Bundled material | 25 `pypa-*.rst` specification pages under `steps/*/workdir/SPECS/specs/` |
| Project | CPython / python-build-standalone |
| URL | https://github.com/astral-sh/python-build-standalone |
| Releases pinned | CPython 3.9.25, 3.10.21, 3.11.16, 3.12.14, 3.13.15 |
| Bundled material | `sysconfigdata.py` dumps under `steps/*/tests/pythons-payload/`, generated from the pinned `install_only_stripped` builds |

### Bundled and derived material

* `steps/01-pip-compatible-core/solution/uv-0.1.0-tree.tar.gz` — the complete
  uv source tree at tag 0.1.0 (no `.git` directory), including
  `LICENSE-APACHE`, `LICENSE-MIT`,
  `crates/pep440-rs/{License-Apache,License-BSD}` and
  `crates/pep508-rs/{License-Apache,License-BSD}`. The tree also contains
  third-party-derived code: `crates/uv-extract/src/vendor/cloneable_seekable_reader.rs`
  (vendored from Google's `ripunzip`, `Copyright 2022 Google LLC`,
  Apache-2.0 OR MIT), `crates/uv-client/src/rkyvutil.rs` (a serializer largely
  adapted from rkyv's `CompositeSerializer`, MIT, `Copyright 2021 David
  Koloski`) and `crates/platform-host/src/linux.rs` (which, besides the
  glibc-version-rs code, contains a musl-version parser marked “Taken from
  maturin”, Apache-2.0 OR MIT, `Copyright (c) 2018 konstin`).
* `steps/02…/solution/uv-0.1.0-to-0.1.24.patch.gz` through
  `steps/15…/solution/uv-0.12.0-to-0.12.17.patch.gz` — gzip-compressed unified
  diffs between consecutive upstream releases. They carry upstream source
  only; the benchmark adds nothing to them.
* `steps/*/workdir/SPECS/specs/` — verbatim reStructuredText originals of 20
  PEPs and 25 PyPA specification pages. `MANIFEST.tsv` records the source URL,
  pinned commit, retrieval time, SHA-256 and per-document licence;
  `PROVENANCE.json` records the two commits. Per-step document sets grow from
  37 documents at step 1 to 45 at step 15.
* `steps/*/workdir/SPECS/changelog/` — excerpts of uv `CHANGELOG.md` and
  `changelogs/0.N.x.md` at each pinned release, restricted to release-note
  prose; the selection rules are recorded in
  `steps/01-pip-compatible-core/workdir/SPECS/licences/NOTICE-uv-changelog.txt`.
* `steps/*/tests/pythons-payload/cpython-<version>-linux-x86_64-gnu.sysconfigdata.py`
  — the `sysconfig` variable dump produced by each pinned interpreter (the
  `install_only_stripped` archives recorded in `tests/pythons.sha256`). This
  output is derived from CPython and is covered by PSF-2.0.
* The scaffold (`Cargo.toml`, `Cargo.lock`, `pyproject.toml`, `TEST_INTERFACE.md`,
  `WORKSPACE_HANDOFF.md`, the harnesses and probes) is benchmark-authored.

## License table

| SPDX ID | File under `LICENSES/` | Applies to |
|---|---|---|
| Apache-2.0 | `Apache-2.0.txt` | uv, all pinned releases. Exact `LICENSE-APACHE` from tag 0.12.17 (SHA-256 `c71d239d…`); uv is dual-licensed Apache-2.0 OR MIT. Also covers the Apache-2.0 option of `pep440-rs` 0.4.0, `pep508-rs` 0.2.3, the vendored `cloneable_seekable_reader.rs` from `ripunzip` and the maturin-derived musl parser in `crates/platform-host/src/linux.rs`. |
| MIT | `MIT.txt` | uv, all pinned releases, MIT option. Exact `LICENSE-MIT` from tag 0.12.17 (SHA-256 `860e3d7a…`). The 0.1.0 tree carries the same text with `Copyright (c) 2023 Astral Software Inc.`; the MIT-option side of the `ripunzip` file and the maturin snippet are also covered. |
| MIT | `MIT-rkyv.txt` | `crates/uv-client/src/rkyvutil.rs`, whose custom serializer is largely copied from rkyv's `CompositeSerializer`. Exact rkyv 0.7.44 `LICENSE`, with `Copyright 2021 David Koloski`. |
| BSD-2-Clause | `BSD-2-Clause.txt` | `pep440-rs` 0.4.0 and `pep508-rs` 0.2.3, BSD-2-Clause option. Exact `crates/pep440-rs/License-BSD` from the 0.1.0 tree (identical to `crates/pep508-rs/License-BSD`). |
| BSD-3-Clause | `BSD-3-Clause.txt` | `crates/uv-client/src/remote_metadata.rs`, whose metadata-reading method is derived from `prefix-dev/rip`. The full BSD-3-Clause text, with `Copyright (c) 2023, prefix.dev GmbH`, is embedded verbatim in that source file. |
| Apache-2.0 | `Apache-2.0-glibc-version-rs.txt` | `crates/platform-host/src/linux.rs`, taken from `delta-incubator/glibc-version-rs` under Apache-2.0 “(but not the MIT license)”. Exact upstream `LICENSE.txt`, with `Copyright (2020) QP Hou and a number of other contributors.  All rights reserved.` |
| CC-BY-SA-3.0 | `CC-BY-SA-3.0.txt` | The 25 PyPA Packaging User Guide specification pages. The licence is stated in the packaging.python.org `README.rst`: “The Python Packaging User Guide is licensed under a Creative Commons Attribution-ShareAlike license: http://creativecommons.org/licenses/by-sa/3.0 .” |
| PSF-2.0 | `PSF-2.0.txt` | The CPython `sysconfigdata.py` files for 3.9.25, 3.10.21, 3.11.16, 3.12.14 and 3.13.15. Exact PSF License Version 2 from CPython 3.13.15. |
| CC0-1.0 | `CC0-1.0.txt` | PEPs that offer the CC0-1.0-Universal option: 11 state the dual licence in their text and `pep-0600.rst` is covered by the PEP 1 policy, which places every PEP under public domain / CC0-1.0-Universal. |
| Public-Domain | `Public-Domain.txt` | The public-domain dedication and the exact per-PEP copyright statements, including the PEP 1 dual-licence policy. |

The in-tree copies `steps/01-pip-compatible-core/workdir/SPECS/licences/uv-LICENSE-APACHE`
and `uv-LICENSE-MIT` are byte-identical to `LICENSES/Apache-2.0.txt` and
`LICENSES/MIT.txt`; the tarball's `LICENSE-APACHE` and `LICENSE-MIT` differ
only in the MIT copyright year (2023) and a trailing newline in the Apache
text. The same 0.1.0-era pair is shipped in every step's
`tests/skeleton/starter/` directory.

`pep440-rs`/`pep508-rs` ship an Apache text without the APPENDIX boilerplate;
`LICENSES/Apache-2.0.txt` is the full upstream uv copy, which includes it.

## Modifications made by the benchmark

* The uv source tree is shipped verbatim at tag 0.1.0 with `.git` removed. The
  step-1 oracle installs it over the seeded scaffold; no upstream file is
  edited.
* Steps 2–15 apply the exact upstream release-to-release diffs named above; no
  benchmark-specific change is made to the upstream code they carry.
* The specification documents are copied verbatim. No page text is altered;
  the only benchmark-side selection is which documents each step ships, as
  recorded in `SPECS/MAP.md` and each step's `MANIFEST.tsv`.
* The changelog excerpts are a subset of upstream release notes: documentation,
  CI, release-engineering, performance and Rust-library housekeeping sections
  are omitted, kept sections are verbatim and in upstream order.
* The `sysconfigdata.py` payloads are generated output of the pinned
  interpreters; the interpreter archives themselves are fetched at test time
  from the URLs in `tests/pythons.sha256` and are not committed.
* The scaffold, harnesses, probes and reward logic are benchmark-authored.

## Third-party dependencies bundled in the task

| Component | Version | License | Bundled path |
|---|---|---|---|
| uv | 0.1.0 | Apache-2.0 OR MIT | `steps/01-pip-compatible-core/solution/uv-0.1.0-tree.tar.gz` |
| uv | 0.1.24, 0.2.0, 0.3.0, 0.4.20, 0.5.0, 0.5.20, 0.6.0, 0.7.0, 0.8.0, 0.9.0, 0.10.0, 0.11.0, 0.12.0, 0.12.17 | Apache-2.0 OR MIT | `steps/02…15/solution/*.patch.gz` (upstream diffs) |
| pep440-rs | 0.4.0 | Apache-2.0 OR BSD-2-Clause | `crates/pep440-rs/` inside the 0.1.0 tree |
| pep508-rs | 0.2.3 | Apache-2.0 OR BSD-2-Clause | `crates/pep508-rs/` inside the 0.1.0 tree |
| cloneable_seekable_reader.rs (from `google/ripunzip`) | vendored file, 0.1.0 tree | Apache-2.0 OR MIT | `crates/uv-extract/src/vendor/` inside the 0.1.0 tree |
| rkyv `CompositeSerializer`-derived code | 0.7.44 | MIT | `crates/uv-client/src/rkyvutil.rs` |
| maturin-derived musl parser | source snippet, no declared dependency | Apache-2.0 OR MIT | `crates/platform-host/src/linux.rs` |
| Python Packaging User Guide specifications | 2026-09-09 snapshot, commit `457e74f03e630c7d4414bb39e8e77d4b61df6ee9` | CC-BY-SA-3.0 | `steps/*/workdir/SPECS/specs/pypa-*.rst` |
| Python Enhancement Proposals | 2026-09-18 snapshot, commit `8766f78fee7b2bd08bce0537fcb2c9b103b10d71` | public domain or CC0-1.0-Universal | `steps/*/workdir/SPECS/specs/pep-*.rst` |
| CPython `sysconfigdata` | 3.9.25, 3.10.21, 3.11.16, 3.12.14, 3.13.15 | PSF-2.0 | `steps/*/tests/pythons-payload/*.sysconfigdata.py` |
| uv release notes (excerpts) | tags 0.1.0–0.12.17 | Apache-2.0 OR MIT | `steps/*/workdir/SPECS/changelog/` |

The image prefetches crates from crates.io for offline builds, including
`pep440_rs` 0.3.12 and 0.5.0 and `pep508_rs` 0.2.3 and 0.4.2; those registry
packages (Apache-2.0 OR BSD-2-Clause) are fetched when the image is built and
are not redistributed in this repository. All other crates named by the step
lockfiles are likewise fetched at image-build time.

## Required notices

uv MIT licence, reproduced in `LICENSES/MIT.txt` from tag 0.12.17:

```
Copyright (c) 2025 Astral Software Inc.
```

The 0.1.0 tree carries `Copyright (c) 2023 Astral Software Inc.`; the licence
text is otherwise identical.

BSD-2-Clause copyright line, reproduced in `LICENSES/BSD-2-Clause.txt`:

```
Copyright (c) 2023 konstin
```

Vendored `cloneable_seekable_reader.rs`, dual-licensed Apache-2.0 OR MIT,
carries the header `Copyright 2022 Google LLC`. The rkyv-derived serializer
retains `Copyright 2021 David Koloski` (MIT, reproduced in
`LICENSES/MIT-rkyv.txt`). The maturin-derived musl parser is covered by the
Apache-2.0 option; maturin's MIT copyright line is `Copyright (c) 2018
konstin`.

BSD-3-Clause attribution embedded in `crates/uv-client/src/remote_metadata.rs`
and reproduced in `LICENSES/BSD-3-Clause.txt`:

```
BSD 3-Clause License

Copyright (c) 2023, prefix.dev GmbH
```

Apache-2.0 copyright line for glibc-version-rs, reproduced in
`LICENSES/Apache-2.0-glibc-version-rs.txt`:

```
Copyright (2020) QP Hou and a number of other contributors.  All rights reserved.
```

CC-BY-SA-3.0 attribution for the PyPA pages: “Python Packaging Authority
(PyPA), https://packaging.python.org/”, with the copyright statement
“Copyright 2013-2020, PyPA” as given in the rendered site footer. The copies
are unmodified; any adaptation must be shared under CC-BY-SA-3.0 with
attribution to PyPA.

PSF-2.0 copyright notice: the bundled 3.13.15 interpreter is covered by
“Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved”;
3.9.25 through 3.12.14 use the enumerated form “Copyright (c) 2001, 2002,
2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2015,
2016, 2017, 2018, 2019, 2020, 2021, 2022, 2023 Python Software Foundation; All
Rights Reserved”. The PSF License grants no trademark rights; “Python” and the
Python logo are trademarks of the Python Software Foundation.

PEP public-domain statements are quoted in `LICENSES/Public-Domain.txt` and
`LICENSES/CC0-1.0.txt`. Apache-2.0 is a trademark of the Apache Software
Foundation; no endorsement is implied.

---

Task-authored benchmark content is licensed separately by the benchmark
maintainers.
