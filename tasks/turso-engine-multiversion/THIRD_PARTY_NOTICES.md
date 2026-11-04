# Third-Party Notices

**Task:** `harbor/turso-engine-multiversion` — build a SQLite-file-format
compatible embedded database engine in Rust across fifteen upstream turso
releases, from v0.0.13 through v0.1.5.

## 1. Upstream project

| | |
| --- | --- |
| Project | turso (formerly Limbo) |
| Repository | <https://github.com/tursodatabase/turso> (originally `tursodatabase/limbo`) |
| Upstream licence | MIT (upstream `LICENSE.md`) |
| Copyright | Copyright 2024 the Limbo authors (v0.0.13-v0.0.22); Copyright 2024 the Turso authors (v0.1.0-v0.1.5) |

Pinned releases, in step order:

| Step | Upstream tag | Copyright holder in `LICENSE.md` |
| --- | --- | --- |
| 01-baseline-engine | `v0.0.13` | the Limbo authors |
| 02-checkpointing-and-json | `v0.0.14` | the Limbo authors |
| 03-transactions-and-ctes | `v0.0.15` | the Limbo authors |
| 04-btree-balancing | `v0.0.16` | the Limbo authors |
| 05-schema-mutation-and-jsonb | `v0.0.17` | the Limbo authors |
| 06-wal-integrity | `v0.0.18` | the Limbo authors |
| 07-index-creation | `v0.0.19` | the Limbo authors |
| 08-index-maintenance | `v0.0.20` | the Limbo authors |
| 09-set-operations-and-collations | `v0.0.21` | the Limbo authors |
| 10-alter-table-and-checkpoint-modes | `v0.0.22` | the Limbo authors |
| 11-rename-and-database-header | `v0.1.0` | the Turso authors |
| 12-vector-values | `v0.1.2` | the Turso authors |
| 13-change-data-capture | `v0.1.3` | the Turso authors |
| 14-views | `v0.1.4` | the Turso authors |
| 15-encryption | `v0.1.5` | the Turso authors |

## 2. What is bundled or derived

- `steps/01-baseline-engine/solution/baseline-v0.0.13.tar.gz` — the complete
  upstream v0.0.13 source tree, including the engine, CLI, `vendored/
  sqlite3-parser`, `LICENSE.md`, `NOTICE.md` and the upstream `licenses/`
  notice directory.
- `steps/02-...` through `steps/15-.../solution/release-*-to-*.patch` — exact
  upstream release-to-release source diffs, from v0.0.13 to v0.1.5.
- `steps/*/workdir/spec/turso/` — the documents that release ships
  (`LICENSE.md`, `README.md`, `COMPAT.md`, `CHANGELOG.md`, `PROVENANCE.md`,
  `RELEASE_TAG`, and the manual/config pages where present), extracted
  verbatim from the upstream tag.
- `steps/*/workdir/spec/sqlite/` — 54 pages of SQLite documentation, retrieved
  byte-for-byte from <https://www.sqlite.org> (public domain).
- `environment/sqlite-src/` — the SQLite 3.50.2 amalgamation (`sqlite3.c`,
  `sqlite3.h`, `shell.c`), public domain, used only as a verifier oracle.
- Benchmark-authored: `instruction.md`, `tests/`, `spec/README.md`,
  `spec/PROFILE.md`, `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md`, `timer.sh`.

## 3. Licence files

| SPDX ID | File under `LICENSES/` | Upstream component / versions covered |
| --- | --- | --- |
| MIT | `MIT-Limbo.txt` | turso `v0.0.13` through `v0.0.22` (upstream `LICENSE.md`, Copyright 2024 the Limbo authors) |
| MIT | `MIT-Turso.txt` | turso `v0.1.0` through `v0.1.5` (upstream `LICENSE.md`, Copyright 2024 the Turso authors) |
| Public domain | `Public-Domain.txt` | SQLite 3.50.2 amalgamation and SQLite documentation |
| Unlicense | `Unlicense.txt` | `vendored/sqlite3-parser` bundled in the upstream tree |

`MIT-Limbo.txt` was copied byte-for-byte from
`steps/01-baseline-engine/workdir/spec/turso/LICENSE.md` (the v0.0.13
edition); `MIT-Turso.txt` from
`steps/15-encryption/workdir/spec/turso/LICENSE.md` (the v0.1.5 edition).
Both match the upstream files at their respective tags.

`Public-Domain.txt` records the SQLite public-domain dedication from
<https://www.sqlite.org/copyright.html> and the upstream blessing carried in
the `sqlite3.h` header. The in-tree provenance note is
`environment/sqlite-src/LICENCE.md`.

`Unlicense.txt` is `vendored/sqlite3-parser/LICENSE` from the v0.0.13 baseline
archive, copied byte-for-byte.

## 4. Modifications made by this benchmark

- The baseline tree is the complete upstream v0.0.13 tree; later steps apply
  exact upstream release-to-release patches. No upstream source file is edited
  by the benchmark.
- The Oracle build uses `cargo build --no-default-features` to avoid the
  `io_uring` backend, which Docker's default seccomp profile blocks. This is a
  build invocation, not a source change.
- `Cargo.lock` files are pinned for a reproducible `--locked` build.
- The `spec/turso/` documents are extracted verbatim; `spec/README.md` and
  `spec/PROFILE.md` are benchmark-authored and identify themselves as such.
- The SQLite documentation pages are stored byte-for-byte as served and are
  not edited.

## 5. Third-party dependencies bundled in the task

| Component | Version | Licence | Bundled path |
| --- | --- | --- | --- |
| SQLite | 3.50.2 amalgamation | Public domain | `environment/sqlite-src/sqlite3.c`, `sqlite3.h`, `shell.c` |
| SQLite documentation | 54 pages retrieved 2026-09-20 | Public domain | `steps/*/workdir/spec/sqlite/` |
| sqlite3-parser | as vendored in turso v0.0.13 | Unlicense | `vendored/sqlite3-parser/` inside `baseline-v0.0.13.tar.gz` |

Build-time Cargo dependencies are not vendored. The upstream tree records
their notices in its `licenses/` directory, which is carried inside the
solution archives: `licenses/core/` for serde (Apache-2.0 OR MIT) and
serde_json5 (Apache-2.0) at v0.0.13, with further `licenses/extensions/`
entries added by later releases (including ipnetwork and libm, both
Apache-2.0 OR MIT). The optional Java-binding notices under
`licenses/bindings/java/` refer to Error Prone, AssertJ, logback and spotless;
the Java bindings themselves are not part of this task's payload. The upstream
`NOTICE.md` labels logback Apache-2.0, while the referenced
`logback-license.md` states that logback is dual-licensed under EPL-1.0 or
LGPL-2.1. Neither logback nor the Java bindings are bundled here.

## 6. Required notices

- `Copyright 2024 the Limbo authors` for turso v0.0.13 through v0.0.22.
- `Copyright 2024 the Turso authors` for turso v0.1.0 through v0.1.5.
- SQLite public-domain dedication and blessing, reproduced in
  `LICENSES/Public-Domain.txt`.
- The Unlicense dedication for `sqlite3-parser`, reproduced in
  `LICENSES/Unlicense.txt`.
- turso publishes a `NOTICE.md`, but it is a dependency index rather than an
  Apache NOTICE file; it is included verbatim in the upstream tree at
  `NOTICE.md` inside `baseline-v0.0.13.tar.gz`.
- No trademark licence is granted by the upstream or by this task.

## 7. Scope of this licence bundle

This bundle covers the upstream turso material, the bundled sqlite3-parser,
and the SQLite amalgamation and documentation redistributed with this task.
Task-authored benchmark content is licensed separately by the benchmark
maintainers.
