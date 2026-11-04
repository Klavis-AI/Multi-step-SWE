# Third-party notices — sql-database-multiversion

Implement a SQLite-compatible relational database in C across five public
SQLite releases, from the 3.8.0 core engine through CTEs, JSON1, UPSERT and
window functions, graded through the `sqlite3` CLI and the documented C API.

## Upstream project

| | |
|---|---|
| Project | SQLite |
| URL | https://sqlite.org/ |
| Releases pinned | 3.8.0 (2013-08-26), 3.8.3 (2014-02-03), 3.9.0 (2015-10-14), 3.24.0 (2018-06-04), 3.25.0 (2018-09-15) |
| Release identifiers | `f64cd21e2e23ed7cff48f7dafa5e76adde9321c2` (3.8.0), `6c643e45c274e755dc5a1a65673df79261c774be` (3.8.3), `a721fc0d89495518fe5612e2e3bbc60befd2e90d` (3.9.0), `c7ee0833225bfd8c5ec2f9bf62b97c4e04d03bd9566366d5221ac8fb199a87ca` (3.24.0), `b63af6c3bd33152742648d5d2e8dc5d5fcbcdd27df409272b6aea00a6f761760` (3.25.0) |
| Bundled documentation | A selected subset of the official `sqlite-doc-<release id>.zip` page set for each release, copied byte for byte into `steps/<step>/workdir/sqlite-doc/` (including `c3ref/`) |
| Bundled source | `steps/01-core-engine/solution/sqlite-3.8.0-amalgamation.tar.gz` — the official 3.8.0 amalgamation (`src/sqlite3.c`, `shell/shell.c`, `include/sqlite3.h`, `include/sqlite3ext.h`) |
| Derived source | `steps/<step>/solution/release-<from>-to-<to>.patch` — unified diffs over `src/`, `shell/` and `include/` between consecutive pinned releases |

Every `steps/<step>/workdir/sqlite-doc/LICENSE-NOTICE.md` records the exact
release and `SQLITE_SOURCE_ID` for that step.

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| Public domain | `LICENSES/Public-Domain.txt` | SQLite code and documentation, all five pinned releases |

`LICENSES/Public-Domain.txt` reproduces the statements on SQLite's
`copyright.html` page — the page bundled verbatim in every step's
`sqlite-doc/`. It records that all SQLite code and documentation is dedicated
to the public domain, that anyone is free to copy, modify, publish, use,
compile, sell or distribute it for any purpose, and that the deliverable code
was written from scratch. Some build scripts (for example autoconf-generated
`configure`) may carry other open-source licences; none of them is bundled in
this task.

## Version-to-license map

| Step | SQLite release | Documentation | Solution payload |
|---|---|---|---|
| `01-core-engine` | 3.8.0 | `sqlite-doc/` subset for 3.8.0 | 3.8.0 amalgamation tarball |
| `02-cte-and-values` | 3.8.3 | `sqlite-doc/` subset for 3.8.3 | 3.8.0 → 3.8.3 patch |
| `03-json-and-expression-indexes` | 3.9.0 | `sqlite-doc/` subset for 3.9.0 | 3.8.3 → 3.9.0 patch |
| `04-upsert` | 3.24.0 | `sqlite-doc/` subset for 3.24.0 | 3.9.0 → 3.24.0 patch |
| `05-window-functions` | 3.25.0 | `sqlite-doc/` subset for 3.25.0 | 3.24.0 → 3.25.0 patch |

The licence is the same public-domain dedication across all five releases;
the version map records which documentation and source payload each step
carries.

## Modifications made by the benchmark

- Each step's `sqlite-doc/` is an unmodified subset of the official
  documentation archive for that release (70 pages in step 01; the set is not
  the whole upstream site); `SHA256SUMS` covers every copied page.
- `sqlite-doc/README.md`, `sqlite-doc/PROFILE.md` and
  `sqlite-doc/LICENSE-NOTICE.md` are authored by the task, not upstream
  documents.
- The step-01 solution installs the official 3.8.0 amalgamation unchanged;
  the task-owned `Makefile` is the only build file added.
- The later steps' solution patches are unified diffs between the official
  release amalgamations; `solve.sh` applies them with `git apply --binary`.
- The seeded `/app` scaffold (`src/`, `shell/`, `include/` READMEs, the
  `Makefile`, `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md`, `timer.sh`),
  harnesses, tests and instructions are authored benchmark content. No SQLite
  source is present in the starting workspace.

## Third-party dependencies bundled in the task

| Component | Version | License | Bundled path |
|---|---|---|---|
| SQLite documentation | 3.8.0, 3.8.3, 3.9.0, 3.24.0, 3.25.0 | Public domain | `steps/<step>/workdir/sqlite-doc/` |
| SQLite amalgamation | 3.8.0 | Public domain | `steps/01-core-engine/solution/sqlite-3.8.0-amalgamation.tar.gz` |
| SQLite release diffs | 3.8.0→3.8.3, 3.8.3→3.9.0, 3.9.0→3.24.0, 3.24.0→3.25.0 | Public domain | `steps/<step>/solution/release-*.patch` |

No other third-party component is bundled. The grading verifier invokes a
`sqlite3` CLI from its own image; no SQLite binary is shipped in the task tree.

## Required notices

SQLite's `copyright.html` states:

> SQLite is in the Public Domain
>
> All of the code and documentation in SQLite has been dedicated to the public
> domain by the authors. All code authors, and representatives of the companies
> they work for, have signed affidavits dedicating their contributions to the
> public domain and originals of those signed affidavits are stored in a
> firesafe at the main offices of Hwaci. Anyone is free to copy, modify,
> publish, use, compile, sell, or distribute the original SQLite code, either
> in source code form or as a compiled binary, for any purpose, commercial or
> non-commercial, and by any means.

The page also carries the contributor dedication:

> The author or authors of this code dedicate any and all copyright interest in
> this code to the public domain. We make this dedication for the benefit of the
> public at large and to the detriment of our heirs and successors. We intend
> this dedication to be an overt act of relinquishment in perpetuity of all
> present and future rights to this code under copyright law.

The task is not affiliated with or endorsed by the SQLite project or Hwaci.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
