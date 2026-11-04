# The Icechunk specification corpus — milestone 09

This milestone ships sixty-seven files — the project's own documentation, copied
verbatim from `earth-mover/icechunk` at the tag it is anchored to, plus its
`LICENSE`, a `NOTICE` recording the provenance and licence, and a `SHA256SUMS`
checksumming every file that was copied. `README.md` and `PROFILE.md` are the
two the benchmark owns.

This is a reference corpus. It contains no implementation, and nothing in it is
generated from the code you are writing. Do not edit it.

```
icechunk-spec/
├── README.md          this file
├── PROFILE.md         the benchmark's resolutions; read it second
├── NOTICE, LICENSE, SHA256SUMS
├── spec.md            the normative format specification at this release
│                      (version 2; identical to reference/spec.md)
├── archive/           1 file  — the original spec-1 document
├── design/           16 files — the project's design documents (5 new here)
├── flatbuffers/       6 files — the normative binary schemas (repo.fbs is new)
├── guide/            24 files — the prose documentation
└── reference/        16 files — the published specification generations and the
                                 API reference pages (the whole directory is new
                                 here)
```

## Read this first: the directory also holds older milestones' copies

Each milestone's documents are laid **over** the previous milestone's, not
substituted for them. Because this release renamed most of the documentation
site, `/app/icechunk-spec/` therefore contains **thirty-four extra files from
earlier milestones under names this release no longer uses**, and they are
stale. They are not listed in the tree above and not covered by this
milestone's `SHA256SUMS`.

Where two files cover the same subject, **the one named in the tree above is the
one that belongs to this release.** In particular:

| stale, ignore | current |
| --- | --- |
| `guide/spec.md` — **the version-1 specification, titled only "Icechunk Specification"** | `spec.md` at the top level, and `reference/spec.md`, for version 2; `reference/spec-v1.md` for version 1 |
| `guide/version-policy.md`, `guide/migration.md`, `guide/icechunk-rust.md` | the same files under `reference/` |
| `guide/quickstart.md`, `howto.md` | `guide/getting-started-*.md` |
| `guide/async.md`, `configuration.md`, `dask.md`, `icechunk-for-git-users.md`, `performance.md`, `storage.md`, `virtual.md`, `xarray.md` | `guide/guides-*.md` |
| `guide/concepts.md`, `expiration.md`, `faq.md`, `parallel.md`, `version-control.md` | `guide/understanding-*.md` |
| `guide/concurrency.md` | `guide/guides-flaky-networks.md` |
| `guide/icechunk-python-*.md` (13 files, from milestones 01–05) | the `guide/` files above |
| `flatbuffers/object_ids.fbs` | `flatbuffers/common.fbs` |

`guide/spec.md` is the trap worth naming twice: it is this ladder's **previous**
specification, it does not describe the repo info object, and reading it instead
of `spec.md` will cost you the milestone.

## Where to read first

For **this** milestone, in order:

1. `PROFILE.md` — what the corpus leaves open, where it describes a Python API
   that has no Rust counterpart, and the one resolution carried forward from
   milestone 08 that this release **reverses**.
2. `reference/spec.md` (= `spec.md`) — the whole of it. This is the milestone.
   The sections that matter most are `### Storage Operations` (the repo info
   object is the only mutable object), `### Consistency and Optimistic
   Concurrency` (fetch, change in memory, conditional write, retry — and which
   failures are fatal rather than retryable), `#### Repo Info File` with its
   `##### Ref`, `##### SnapshotInfo`, `##### Update`, `##### RepoStatus` and
   `##### Updates to the repo info file` sub-sections, `### File Layout`,
   `## Algorithms` (`Initialize New Repository`, `From Branch`,
   `Write New Snapshot`, `Create New Tag`), and `## Changes from spec version 1`,
   which is the shortest complete statement of what this milestone changes on
   disk.
3. `reference/spec-v1.md` — the first-generation format, unchanged and still
   required. Compare its file layout with the new one: `refs/` and `config.yaml`
   are gone, `repo` and `overwritten/` are new, and a snapshot no longer stores
   its own parent.
4. `reference/migration-2.md` — the in-place upgrade: what it reads, what it
   writes, `dry_run`, `delete_unused_v1_files`, `prefetch_concurrency`, and the
   guarantee that it touches no chunks.
5. `reference/breaking-changes-2.md` — the API changes. It is written for the
   Python package; `PROFILE.md` §4 maps each entry onto the Rust surface, and
   `/app/TEST_INTERFACE.md` §19 is the authoritative Rust list.
6. `guide/understanding-repository-features.md` — **new at this release**, and
   the document behind four of this milestone's capabilities: repository status,
   repository metadata, the operations log (including the full `UpdateType`
   table) and feature flags.
7. `guide/understanding-version-control.md` — changed. The new material is
   `## Amending a snapshot`, `### Empty Snapshots` (under
   `## Creating a snapshot`) and `### Creating Anonymous Snapshots` (under
   `## Branches`).
8. `guide/guides-moving-nodes.md` and `guide/guides-moving-chunks.md` — **both
   new**. The first states that a move is metadata-only, needs a rearrange
   session, carries a group's children with it and refuses to overwrite. The
   second states that `shift_array` and `reindex_array` offsets are in **chunks,
   not elements**, and what a backward function buys you.
9. `guide/guides-zarr.md` — **new**. `## Rectilinear chunk grids` is the section
   that matters; the rest is Zarr-level orientation.
10. `guide/guides-virtual.md` — changed. `## Relative Virtual Chunk Containers`
    is new: named containers and `vcc://name/path` locations.
11. `flatbuffers/repo.fbs` — **new**, and normative: the binary shape of the repo
    info object. `flatbuffers/common.fbs` is `object_ids.fbs` renamed and
    extended; `snapshot.fbs`, `manifest.fbs`, `transaction_log.fbs` and `all.fbs`
    changed with the format.
12. `design/010-notes-towards-an-IC-2.md` and
    `design/011-ref-and-ancestry-entry-point.md` — the motivation and the design
    for everything above. **Both were present and unimplemented at milestone 08;
    this is the release that implements them.**

## The rest of the corpus

`reference/` also holds the **API reference pages** of the documentation site.
Seven of them —
`reference/{ops,conflicts,session,snapshots,config,virtual,storage}.md` — plus
`reference/credentials.md` are **80-to-170-byte mkdocstrings include shims**: a
title, a one-line summary and a `::: icechunk.<module>` directive that the site
expands from Python docstrings at build time. They contain **no prose and no
requirement**. The real prose lives in `guide/`, and the Rust contract lives in
`/app/TEST_INTERFACE.md`. Do not go looking for a specification in them; there
is not one. `reference/icechunk-rust.md` (327 bytes) is a similar stub for the
Rust crate, and `reference/index.md` is the reference section's table of
contents.

The substantial documents under `reference/` are exactly four: `spec.md`,
`spec-v1.md`, `migration-2.md` and `breaking-changes-2.md`, plus
`reference/migration.md` (the pre-1.0 migration guide, historical) and
`reference/version-policy.md` (the compatibility policy introduced at milestone
08 and moved under `reference/` here; upstream also reworded its support-window
paragraph and two of its headings between the two releases, so it is not
byte-identical to milestone 08's copy).

The other guide documents cover the surface earlier milestones built and this
one must preserve: `getting-started-quickstart.md` and `getting-started-howto.md`
(orientation), `understanding-concepts.md` (the repository/session/snapshot
model), `understanding-expiration.md` (expiration and garbage collection),
`understanding-parallel.md` and `guides-dask.md` (distributed writes),
`guides-async.md` (the asynchronous interface), `guides-configuration.md`,
`guides-performance.md`, `guides-flaky-networks.md` (retries and timeouts),
`guides-storage.md` (the backends, including the new HTTP and redirect ones),
`guides-icechunk-for-git-users.md`, `guides-xarray.md`, `understanding-faq.md`,
`overview.md`, `sample-datasets.md`, `related-projects.md` and `index.md`.

`design/` holds the project's design documents. They are motivation, not
behaviour, and several describe designs that were never implemented as written —
`design/005-manifest-split.md` most obviously. New here:
`011-IC2-gc-and-expiration-consistency.md`, `012-some-more-IC2-format-changes.md`,
`013-feature-flags.md`, `014-virtual-chunk-ref-efficiency.md` and
`015-extra-data-in-manifests-and-snapshots.md`.
`design/004-binary-format.md` is still the closest thing to a written account of
the 39-byte object header. `archive/spec-alpha1.md` is the original long
specification, kept for historical reference; where it and `spec.md` disagree,
`spec.md` wins.

## What the corpus does not tell you

The corpus is the project's user documentation, not its Rust API reference, and
this release widened that gap: the Python package was reorganised into
submodules, several Python names have no Rust counterpart at all, and the
`reference/` API pages are generated stubs. Nothing in the corpus states the
Rust names, signatures or error types the verifier uses.

In particular the corpus never states: the workspace split into six crates and
which `icechunk::` paths survive it; that `Session::commit` returns a builder;
that there is no Rust `ForkSession`; the tuple order of the operations-log
stream; that `DimensionShape::new`'s second argument changed meaning; that the
blanket `From` impls on `ICError` were deleted; the defaults of
`RepoUpdateRetryConfig`; or the command-line interface, which no guide document
describes at all.

`/app/TEST_INTERFACE.md` is the authority for every one of those, and
`PROFILE.md` records where the two documents meet.
