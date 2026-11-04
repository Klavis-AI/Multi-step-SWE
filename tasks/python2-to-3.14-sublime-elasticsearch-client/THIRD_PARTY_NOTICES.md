# Third-Party Notices

## Task

`python2-to-3.14-sublime-elasticsearch-client` — climb the Sublime Text
Elasticsearch Client from its Python 2/3 straddle to Python 3.14, one rung at a
time, preserving its recorded behaviour.

## Upstream project

| Field | Value |
| --- | --- |
| Project | Sublime Text Elasticsearch Client |
| URL | <https://github.com/KunihikoKido/sublime-elasticsearch-client> |
| Pin | commit `09e9396502a9de9b5e7aa96112c7515330891377` |
| Bundled path | `environment/repo/` — the complete repository at the pin, including the vendored `lib/` tree |
| Upstream license | MIT; copyright line `Copyright (c) 2015 Kunihiko Kido` (`environment/repo/LICENSE`) |

## License files

| SPDX ID | File under `LICENSES/` | Covers |
| --- | --- | --- |
| MIT | `MIT.txt` | Sublime Text Elasticsearch Client at the pin. Exact copy of `environment/repo/LICENSE`. |
| Apache-2.0 | `Apache-2.0.txt` | requests 2.7.0 and elasticsearch-py 1.6.0. Canonical Apache License 2.0 text, including the appendix. |
| MIT | `MIT-urllib3.txt` | urllib3 1.10.4. Exact upstream `LICENSE.txt`. |
| MIT | `MIT-six.txt` | six 1.9.0. Exact upstream `LICENSE`. |
| MIT | `MIT-six-1.2.0.txt` | six 1.2.0, nested inside the vendored urllib3 copies. Text of the license header embedded in `six.py`. |
| BSD-3-Clause | `BSD-3-Clause.txt` | python-dateutil 2.4.2. Exact upstream `LICENSE`, including all three copyright lines. |
| MIT | `MIT-analytics-python.txt` | analytics-python 1.1.0. SPDX canonical MIT text; see the provenance note below. |
| LGPL-2.1 | `LGPL-2.1.txt` | chardet 2.3.0, nested inside the vendored requests. Exact upstream `LICENSE`. |

## Vendored third-party dependencies

All of the following are shipped inside `environment/repo/lib/`; the paths are
relative to that directory.

| Component | Version | License | Bundled path |
| --- | --- | --- | --- |
| requests | 2.7.0 | Apache-2.0; upstream `LICENSE` carries `Copyright 2015 Kenneth Reitz` | `requests/` |
| urllib3 | 1.10.4 | MIT; `Copyright 2008-2014 Andrey Petrov and contributors` | `urllib3/` and `requests/packages/urllib3/` |
| elasticsearch-py | 1.6.0 | Apache-2.0 | `elasticsearch/` |
| six | 1.9.0 | MIT; `Copyright (c) 2010-2015 Benjamin Peterson` | `six.py` |
| six (nested) | 1.2.0 | MIT; `Copyright (c) 2010-2011 Benjamin Peterson` | `urllib3/packages/six.py` and `requests/packages/urllib3/packages/six.py` |
| python-dateutil | 2.4.2 | BSD-3-Clause; `Copyright (c) 2003-2011 Gustavo Niemeyer`, `Copyright (c) 2012-2014 Tomi Pieviläinen`, `Copyright (c) 2014 Yaron de Leeuw` | `dateutil/` |
| analytics-python | 1.1.0 | MIT, declared in the 1.1.0 package metadata (`setup.py`: `license='MIT License'`; `PKG-INFO`: `License: MIT License`); the sdist contains no `LICENSE` file and states no copyright line | `analytics/` |
| chardet | 2.3.0 | LGPL-2.1 | `requests/packages/chardet/` |

`environment/repo/requirements.txt` pins `elasticsearch==1.6.0` and
`analytics-python==1.1.0`; the other versions above are those of the vendored
copies.

## Modifications by the benchmark

- `environment/repo/` is the pinned upstream repository, byte-for-byte; the
  image copies it unchanged (see `environment/Dockerfile`).
- Migration steps under `steps/` add benchmark-authored instructions, tests,
  workdirs and solutions; the vendored libraries above are unmodified.

## Required notices

- Sublime Text Elasticsearch Client: `Copyright (c) 2015 Kunihiko Kido` (MIT).
- requests 2.7.0: `Copyright 2015 Kenneth Reitz`. Its upstream `LICENSE` is a
  short Apache-2.0 notice; the full Apache-2.0 text is supplied in
  `LICENSES/Apache-2.0.txt`.
- urllib3 1.10.4: `Copyright 2008-2014 Andrey Petrov and contributors`.
- elasticsearch-py 1.6.0: Apache-2.0; its upstream `LICENSE` is the Apache-2.0
  text.
- six 1.9.0: `Copyright (c) 2010-2015 Benjamin Peterson`; nested six 1.2.0:
  `Copyright (c) 2010-2011 Benjamin Peterson`.
- python-dateutil 2.4.2: the three copyright lines listed in the table above.
- analytics-python 1.1.0: the upstream 1.1.0 sdist states MIT in its package
  metadata but carries no license file or copyright line; `Author` is listed as
  `Segment`.
- chardet 2.3.0: LGPL-2.1; the complete corresponding source of the vendored
  copy is included at `environment/repo/lib/requests/packages/chardet/`.
- The pinned tree contains no upstream `NOTICE` file.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
