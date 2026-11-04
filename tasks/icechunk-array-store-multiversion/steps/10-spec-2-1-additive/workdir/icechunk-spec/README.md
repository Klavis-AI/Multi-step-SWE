# The Icechunk specification corpus — step 10 ("spec 2.1")

Everything in this directory is **documentation**, copied verbatim from the
upstream `earth-mover/icechunk` project. There is **no implementation here** —
no Rust, no Python, no tests. The implementation lives in the cumulative
workspace at `/app`.

Most of it is this milestone's release. Some of it is not: this directory is
**overlaid** on the previous milestone's, not substituted for it, so copies
taken at earlier releases survive under names those releases used. The next
section names them, and none of them is normative here.

Two files are author-owned and are the exception to "verbatim upstream":
this `README.md` and `PROFILE.md`. Everything else is byte-for-byte upstream and
**must not be edited**. `LICENSE` (Apache-2.0) and `NOTICE` carry the terms the
copies are redistributed under; `SHA256SUMS` fixes the digests of the 73 files it
covers — everything in this directory except `README.md`, `PROFILE.md` and
`SHA256SUMS` itself.

## Layout at this step

The first count is what **this milestone copied** — the files `SHA256SUMS`
covers. The second is what you actually see on disk, because earlier
milestones' copies are still here.

```
                        this step   on disk
spec.md                 the CURRENT specification: version 2.1
reference/                  18        19    the published specification
                                            generations and API reference
flatbuffers/                 6         7    the normative binary schemas
design/                     17        17    upstream design documents
guide/                      28        61    prose documentation: guides,
                                            concepts, getting started
archive/                     1         1    the original spec-1 document
LICENSE  NOTICE  SHA256SUMS  README.md  PROFILE.md
```

## Read this first: the directory also holds earlier milestones' copies

Upstream reorganised its documentation site twice while this ladder was
running, and each milestone's corpus is laid **over** the previous one. So
**35 files here were copied at an earlier release, under names this release no
longer uses**. They are stale, they are not listed in this milestone's
`SHA256SUMS`, and `PROFILE.md` R12 declares them non-normative. Where two files
cover the same subject, the one named in the layout above is this release's.

| stale, ignore | current |
| --- | --- |
| `reference/spec.md` — spec **2.0**, presented as the current specification | `spec.md` at the top level, and `reference/spec-v2-1.md`, for 2.1; `reference/spec-v2.md` for the frozen 2.0 |
| `guide/spec.md` — the spec **1** document | as above |
| `flatbuffers/object_ids.fbs` | `flatbuffers/common.fbs` |
| `guide/version-policy.md`, `migration.md`, `icechunk-rust.md` | the same names under `reference/` |
| `guide/quickstart.md`, `howto.md` | `guide/getting-started-*.md` |
| `guide/async.md`, `configuration.md`, `dask.md`, `icechunk-for-git-users.md`, `performance.md`, `storage.md`, `virtual.md`, `xarray.md` | `guide/guides-*.md` |
| `guide/concepts.md`, `expiration.md`, `faq.md`, `parallel.md`, `version-control.md` | `guide/understanding-*.md` |
| `guide/concurrency.md` — a fifteen-line stub of unfilled headings upstream never finished | **no successor.** `guide/guides-flaky-networks.md` is *not* one: it documents storage retry and timeout settings, a different subject |
| `guide/icechunk-python-*.md` (13 files, from milestones 01-05) | the `guide/` files above |

`reference/spec.md` is the one worth naming twice at **this** milestone: it is
the previous generation's document, it says the current specification is version
2, and it has no `## Changes from spec version 2 to 2.1` section. Read the
top-level `spec.md` instead — it is the file this step's instruction points at,
and it is `reference/spec-v2-1.md`.

## Where to start

`spec.md` is the specification this milestone targets: **Icechunk
Specification, version 2.1**. It is the same document as
`reference/spec-v2-1.md` — upstream split its single `reference/spec.md` into
per-generation files at this release, and `spec.md` here is the current one of
them. Read, in order:

1. `spec.md` — `## Changes from spec version 2 to 2.1`. Four bullets; they are
   the whole of the format change.
2. `spec.md` — `##### \`SnapshotInfo\`` under `#### Repo Info File`. What the
   new `pruned_ancestor_tx_logs` field means, how it is ordered, and what a
   reader MUST and MUST NOT assume about it.
3. `spec.md` — `### Binary File Format`. The 39-byte metadata header, field by
   field. The only row that differs from spec 2.0 is the spec-version byte's
   description.
4. `reference/version-policy.md` — `## On-Disk Format` and `### Compatibility`.
   What a *minor* spec version is, why the header byte does not encode it, and
   what "any 2.x library can read and write any spec 2.y repository" obliges.
5. `design/016-expired-transaction-logs.md` — the design behind the field:
   `### Non breaking format change`, `### Population at expiration time`,
   `### GC retention rule`, `### Internal consumers` (diff, amend, rebase,
   inspect), `## Bounded growth`, `## Compatibility`.

## The specification generations

`reference/` carries all three published generations side by side:

| file | what it is |
| --- | --- |
| `reference/spec-v2-1.md` | spec version **2.1** — the current one, identical to `spec.md` |
| `reference/spec-v2.md` | spec version 2, frozen as it was before this release |
| `reference/spec-v1.md` | spec version 1, used by the Icechunk 1.x line |
| `archive/spec-alpha1.md` | the first public spec-1 document, kept for historical reference |

Diffing `reference/spec-v2.md` against `spec.md` is the fastest way to see the
exact extent of the 2.1 change. `reference/spec-v2.md` includes its schema
snippets from a frozen copy of the 2.0 `.fbs` files, so a snippet marker in it
that this corpus does not resolve is expected and is not a missing file.

## The normative schemas

`flatbuffers/` holds `all.fbs`, `common.fbs`, `manifest.fbs`, `repo.fbs`,
`snapshot.fbs` and `transaction_log.fbs` — the schemas `spec.md` includes its
snippets from. `spec.md` writes those includes with upstream's repository-root
paths (`--8<-- "icechunk-format/flatbuffers/repo.fbs:snapshot_info"`); the file
they name is this directory's `flatbuffers/repo.fbs`. At this release:

* `repo.fbs` is the **only** schema with a functional change: one optional
  vector appended to the `SnapshotInfo` table.
* `manifest.fbs` and `snapshot.fbs` are unchanged from spec 2.0.
* `common.fbs` and `transaction_log.fbs` changed **comments only** in the sense
  that no table, field or type declaration moved — but those comments carry a
  normative rule and this release **changed it**. `common.fbs` now declares
  node-path sort order to be the raw UTF-8 byte order of the whole path
  (`/a < /a-b < /a/b`); the previous milestone declared it component-wise
  (`/a < /a/b < /a-b`), which is the opposite answer for those two paths. And
  `transaction_log.fbs` newly declares a **third**, segment-wise order for
  `moved_nodes`, stating that it is *not* the byte order used for snapshot
  nodes. Read both comment blocks; `PROFILE.md` R13 records that neither
  ordering is graded at this milestone.
* `all.fbs` changed only its "how to regenerate" comment.

## The prose documentation

`guide/` is upstream's user-facing documentation, flattened into one directory
with its original section as a filename prefix (`guides-`, `understanding-`,
`getting-started-`). The ones this milestone turns on:

| file | section to read |
| --- | --- |
| `guide/guides-storage.md` | `### Empty prefix and legacy repositories` |
| `guide/guides-configuration.md` | `## Virtual Chunk Credentials` |
| `guide/guides-virtual.md` | virtual chunk containers, including the HTTP and local-filesystem cases |
| `guide/understanding-expiration.md` | `## Expire snapshots`, `## Delete expired data` |
| `guide/guides-observability.md` | `## Logging`, `## OpenTelemetry tracing` |
| `guide/guides-moving-nodes.md` | `## Constraints` |
| `guide/guides-performance.md`, `guide/guides-flaky-networks.md` | inherited behaviour this milestone must not regress |

`reference/changelog.md` is the release log; its
`## Python Icechunk Library 2.1.0` section lists this milestone's features,
deprecations, fixes and breaking changes in one place.
`reference/icechunk-rust.md` points at the Rust crate;
`reference/breaking-changes-2.md` and `reference/migration-2.md` describe the
1.x → 2.0 transition that the workspace has already been through.

Everything else under `reference/` (`config.md`, `conflicts.md`,
`credentials.md`, `index.md`, `migration.md`, `ops.md`, `session.md`,
`snapshots.md`, `storage.md`, `virtual.md`) is the generated Python API
reference. It is useful for *naming and semantics* but it describes the Python
binding, not the Rust surface; where the two differ, `/app/TEST_INTERFACE.md` is
the contract.

## A caution about audience

These documents are written for Icechunk's Python users. Several capabilities
they describe exist only in the Python binding at this release.
`PROFILE.md` records which ones, and `/app/TEST_INTERFACE.md` §11 states what is
deliberately not scored. Read both before concluding that a documented feature
must have a Rust counterpart.
