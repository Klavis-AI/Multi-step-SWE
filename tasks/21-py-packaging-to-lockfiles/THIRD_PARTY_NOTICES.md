# Third-Party Notices

## Task

`21-py-packaging-to-lockfiles` — migrate the Bento Python monorepo from
`setup.py` to PEP 517/518/621 builds, a uv workspace lockfile, reproducible
manylinux/abi3/free-threaded wheels, and PEP 751/639 standards closure.

## Upstream project

| Field | Value |
| --- | --- |
| Project | Bento |
| URL | <https://github.com/cournape/Bento> |
| Pin | commit `37de23d784407a7c98a4a15770ffc570d5f32d70` (presented in the image as Bento 0.2.0) |
| Bundled path | `environment/app/` — full upstream source tree, exported with `git archive` from the pin |
| Upstream license | BSD-3-Clause; copyright line `Copyright (c) 2009, Bento developers` (`environment/app/LICENSE.txt`) |

## License files

| SPDX ID | File under `LICENSES/` | Covers |
| --- | --- | --- |
| BSD-3-Clause | `BSD-3-Clause.txt` | Bento 0.2.0 at commit `37de23d`. Exact copy of `environment/app/LICENSE.txt`. |
| BSD-3-Clause | `BSD-3-Clause-PLY.txt` | PLY 3.3, vendored at `environment/app/bento/private/_ply/ply/`. Text of the license notice embedded in `lex.py` and `yacc.py`. |
| MIT | `MIT-simplejson.txt` | simplejson 2.1.1, vendored at `environment/app/bento/private/_simplejson/simplejson/`. Exact upstream `LICENSE.txt`. |
| MIT | `MIT-six.txt` | six 1.0.0, vendored at `environment/app/bento/private/_six/six.py`. Exact upstream `LICENSE`. |
| PSF-2.0 | `PSF-2.0.txt` | CPython `py_compile.py`-derived bytecode helpers at `environment/app/bento/private/_bytecode_2.py` and `_bytecode_3.py`. SPDX canonical PSF-2.0 text. |

`environment/app/PACKAGERS.txt` records that Bento bundles its non-stdlib
dependencies (PLY 3.3, simplejson, yaku) unmodified so that distributions can
vendor them.

## Bundled third-party dependencies

| Component | Version | License | Bundled path |
| --- | --- | --- | --- |
| PLY | 3.3 | BSD-3-Clause; `Copyright (C) 2001-2009, David M. Beazley (Dabeaz LLC)` | `environment/app/bento/private/_ply/ply/` |
| simplejson | 2.1.1 | MIT; `Copyright (c) 2006 Bob Ippolito` | `environment/app/bento/private/_simplejson/simplejson/` |
| six | 1.0.0 | MIT; `Copyright (c) 2010 Benjamin Peterson` | `environment/app/bento/private/_six/six.py` |
| yaku | 0.0.1 (bundled snapshot) | BSD, declared in `bento/private/_yaku/setup.py`; shipped inside the Bento tree, so the repository BSD-3-Clause license in `LICENSES/BSD-3-Clause.txt` applies | `environment/app/bento/private/_yaku/` |
| CPython `py_compile.py`-derived code | Python 2.7/3.x era | PSF-2.0; the files themselves carry only an `implementation details from py_compile` comment, no copyright header | `environment/app/bento/private/_bytecode_2.py`, `environment/app/bento/private/_bytecode_3.py` |

The image additionally installs `ply`, `six` and `simplejson` from PyPI to warm
the build cache (see `environment/Dockerfile`). Those distributions are not
shipped in the task tree; the source copies listed above are the ones bundled in
the task.

## Modifications by the benchmark

- `environment/app/` is the `git archive` export of the pinned commit. The image
  re-initialises git to present a release checkout tagged `v0.2.0`; the file
  contents are unchanged.
- Migration steps under `steps/` add benchmark-authored workdirs, tests and
  solutions; no upstream file is edited in place.

## Required notices

- Bento: `Copyright (c) 2009, Bento developers` (from `LICENSE.txt`).
- PLY: `Copyright (C) 2001-2009, David M. Beazley (Dabeaz LLC)`.
- simplejson: `Copyright (c) 2006 Bob Ippolito`.
- six: `Copyright (c) 2010 Benjamin Peterson`.
- CPython-derived helpers: PSF-2.0, reproduced in `LICENSES/PSF-2.0.txt`.
- The pinned Bento tree contains no upstream `NOTICE` file.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
