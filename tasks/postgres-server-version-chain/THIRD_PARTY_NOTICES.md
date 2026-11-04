# Third-party notices — postgres-server-version-chain

A ten-step benchmark in which a candidate builds a PostgreSQL-compatible server
in C and carries it from the PostgreSQL 9.6 wire and SQL surface to PostgreSQL
18, one major release per milestone.

## Upstream project

| | |
|---|---|
| Project | PostgreSQL |
| URL | https://www.postgresql.org/ |
| Releases pinned | 9.6.24, 10.21, 11.16, 12.22, 13.23, 14.24, 15.19, 16.15, 17.11, 18.6 |
| Bundled material | PostgreSQL HTML documentation for each pinned release, under `environment/docs/postgresql/`, and benchmark-derived Markdown conversions of selected pages under `steps/*/workdir/SPEC/` |
| Server source | None. The task tree contains no PostgreSQL server, client-library, SPI or procedural-language source. |
| Oracle images | Referenced by digest only (`provenance/images.txt`); no image layer is committed to this repository. |

The documentation was assembled as recorded in
`environment/docs-provenance.txt` and `environment/docs/postgresql/SOURCES.txt`:

* PostgreSQL 9.6–16: `doc/src/sgml/html` extracted from the official release
  tarball (`https://ftp.postgresql.org/pub/source/v<release>/postgresql-<release>.tar.bz2`);
  the rest of each tarball, including the server sources, was discarded.
* PostgreSQL 17 and 18: the `html` directory from the matching PGDG
  `postgresql-doc-NN` package.
* Pages for the client libraries (libpq, ECPG), SPI and the Perl/Python/Tcl
  procedural languages were removed. Every archive is pinned by SHA-256 in
  `SOURCES.txt`, and the resulting tree is checksummed in
  `environment/docs/postgresql/MANIFEST.sha256`.

The `SPEC/*.md` files in each step's workdir are benchmark conversions of pages
from the corresponding release's documentation; they are derived from the same
PostgreSQL documentation and carry the same licence. The step's `SPEC/` is
stripped of site navigation and version-switcher chrome, so no page names a
release later than the step's own.

## License table

| SPDX ID | File under `LICENSES/` | Applies to |
|---|---|---|
| PostgreSQL | `PostgreSQL.txt` | The PostgreSQL License. Exact `COPYRIGHT` file from the PostgreSQL source tree at tag `REL_18_6` (PostgreSQL 18.6). |
| PostgreSQL | `PostgreSQL-Docs-Legal-Notice.txt` | Legal notice shipped in the bundled PostgreSQL 18.6 documentation (`environment/docs/postgresql/18/legalnotice.html`). |
| PostgreSQL | `PostgreSQL-Docs-Legal-Notice-9.6.txt` | Legal notice shipped in the bundled PostgreSQL 9.6.24 documentation (`environment/docs/postgresql/9.6/LEGALNOTICE.html`). |

The PostgreSQL License terms are identical across all ten pinned releases. The
copyright statement differs: 9.6 uses “PostgreSQL is Copyright © 1996-2021”
and “Postgres95 is Copyright © 1994-5”; 10–12 keep that wording with later end
years; 13 uses the “PostgreSQL Database Management System … Portions Copyright
© 1996-2025” form; 14–18 use the same form with 1996-2026. The exact legal
notice for every bundled edition is present at
`environment/docs/postgresql/<release>/legalnotice.html` (9.6:
`LEGALNOTICE.html`). The PostgreSQL `COPYRIGHT` file at tag `REL9_6_24` reads
“formerly known as Postgres, then as Postgres95” and “1996-2021”; the terms
themselves are unchanged.

## Modifications made by the benchmark

* Only documentation is shipped from upstream. The server, client libraries and
  test suites are not present; the candidate implements the server from the
  task's own specification.
* The documentation set is a subset of each release's HTML documentation:
  client-library, SPI and procedural-language pages were dropped, as recorded
  in `environment/docs-provenance.txt`. No surviving page's text was altered.
* `steps/*/workdir/SPEC/*.md` are benchmark-authored Markdown conversions of
  selected documentation pages, with the site navigation and version selector
  removed.
* `WORKSPACE_HANDOFF.md`, `INTERFACE.md`, the test harnesses, the probe corpus
  and the step instructions are benchmark-authored content.
* The oracle images named in `provenance/images.txt` were used to record
  expected behaviour; they are referenced by digest and are not redistributed.

## Third-party dependencies bundled in the task

No third-party source or binaries are committed to the task tree beyond the
PostgreSQL documentation described above. The environment image installs
`postgresql-client-18` (PostgreSQL License), Node.js 24.22.0 (MIT and
third-party licences) and the Python package `psycopg` 3.2.3 (LGPL-3.0-only)
from Debian and PyPI at image build time; those distributions are not part of
this repository.

## Required notices

The PostgreSQL License, as reproduced in `LICENSES/PostgreSQL.txt` from the
PostgreSQL 18.6 source tree, carries these copyright lines:

```
PostgreSQL Database Management System
(also known as Postgres, formerly known as Postgres95)

Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group

Portions Copyright (c) 1994, The Regents of the University of California
```

The bundled documentation's legal notice, reproduced verbatim in
`LICENSES/PostgreSQL-Docs-Legal-Notice.txt` (18.6) and
`LICENSES/PostgreSQL-Docs-Legal-Notice-9.6.txt` (9.6.24), states the same
permission and disclaimer terms.

“PostgreSQL” is a registered trademark of the PostgreSQL Community Association
of Canada. The PostgreSQL License grants no trademark rights beyond reasonable
and customary use in describing the origin of the work.

---

Task-authored benchmark content is licensed separately by the benchmark
maintainers.
