# Reading these documents

Two directories, two provenances. Nothing here contains an implementation of
the engine you are building.

## `sqlite/` — the normative file format and SQL semantics

The SQLite documentation, retrieved from `sqlite.org` and stored exactly as
served. SQLite and its documentation are public domain; see
`sqlite/copyright.html`. `sqlite/PROVENANCE.md` records when each page was
retrieved and `sqlite/SHA256SUMS` records what was received.

These pages are **verbatim and unedited**. They describe stock SQLite, which
is not in every respect what you are building — where the two differ,
`PROFILE.md` in this directory says so and is what the tests assert.

Start here:

| Question | Page |
|---|---|
| What goes in the 100-byte page-1 header, a B-tree page, a cell, a record | `sqlite/fileformat2.html` |
| WAL header, frame header, and the checksum algorithm | `sqlite/fileformat2.html`, section 4 |
| The `-shm` wal-index, its locks, and recovery | `sqlite/walformat.html` |
| How the WAL is used and checkpointed | `sqlite/wal.html`, `sqlite/pragma.html` |
| Statement syntax | `sqlite/lang.html` and the `sqlite/lang_*.html` pages it indexes |
| Type affinity, comparison and sorting rules | `sqlite/datatype3.html` |
| Rowids, `INTEGER PRIMARY KEY`, `WITHOUT ROWID` | `sqlite/rowidtable.html`, `sqlite/withoutrowid.html` |
| The `sqlite_schema` table | `sqlite/schematab.html` |
| Index selection and the query planner | `sqlite/optoverview.html`, `sqlite/queryplanner.html` |
| Virtual tables and `xBestIndex` | `sqlite/vtab.html` |
| JSON functions | `sqlite/json1.html` |
| Behaviour that surprises people | `sqlite/quirks.html`, `sqlite/nulls.html` |

## `turso/` — the upstream project's own documents, at this step's release

`COMPAT.md` is the compatibility matrix: statement by statement, function by
function, what the upstream project claimed to support at this release.
`CHANGELOG.md` and `README.md` describe what the release added. `manual.md`
and `cli-config.md` appear at the releases that have them.

These are a record of a project's claims, not a statement of this task's
scope, and the two disagree in places. Read them alongside the requirement
list in `instruction.md`, which is what is graded: where a `No` row in the
matrix — or, at the releases that ship a manual, a line in the manual's
limitations list — names something the requirement list asks for, the
requirement list wins. Both are descriptions of the release written by the
project; neither was revised for this task, and neither is the contract.

These are extracted verbatim from the upstream repository at the tag named in
`turso/RELEASE_TAG`, MIT licensed; see `turso/LICENSE.md` and
`turso/PROVENANCE.md`. Each step ships only its own release's edition, so no
document in `turso/` describes a capability belonging to a later step. That is
a statement about this directory only. `PROFILE.md`, described below, is
author-owned, is written once for the whole task rather than per release, and
deliberately does name surface belonging to steps other than this one.

## `PROFILE.md` — author-owned

The compatibility profile: the places where the format and the documentation
leave a writer free to choose, and what this task fixes instead. It is the
only document in this directory that is not a verbatim copy of a public
source, along with this README.
