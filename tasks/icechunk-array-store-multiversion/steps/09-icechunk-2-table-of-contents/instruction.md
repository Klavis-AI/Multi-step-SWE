# 09 — Icechunk 2: the repository table of contents

## Implement this milestone

`/app` already holds milestones 01–08 of the `icechunk` transactional array
store. Advance it to its **second on-disk generation**, in which a repository's
references, snapshot index, configuration, metadata, status, feature flags and
operations log all live in a single mutable object at `$ROOT/repo` — the
repository's table of contents — instead of in a `refs/` prefix and a
`config.yaml`. New repositories are written at this generation by default;
first-generation repositories keep opening, reading and writing, and a single
function upgrades one in place.

What this milestone adds, on top of everything already there:

* the table of contents itself, its read accessors, its **pure** mutators, and
  the optimistic-concurrency loop that writes it back;
* an **operations log** covering every mutation, including the ones `ancestry`
  can never show — branches, tags, configuration, metadata, status, feature
  flags, garbage collection, expiration, migration;
* a **commit builder**: `Session::commit` is no longer `async`, and `amend`,
  `anonymous`, `rewrite_manifests`, `rebase` and parallel array flush are its
  methods;
* **anonymous snapshots** and **fork sessions** for distributed writes;
* **metadata-only relabelling**: `move_node` in a dedicated rearrange session,
  and `shift_array` / `reindex_array` as ordinary edits;
* **rectilinear chunk grids**, repository **status**, repository **metadata**,
  **feature flags**, `vcc://` **relative virtual chunk references**, and
  **ancestry graph rendering**;
* the **first-to-second-generation migration**, with a dry run;
* the **workspace split** that makes all of it buildable without async, a
  filesystem or the cloud SDKs — six library crates, re-exported through
  `icechunk` so that almost every existing `icechunk::` path still resolves.

Preserve every earlier milestone: the repository format, sessions and conflict
rebase, expiration and garbage collection, the FlatBuffers objects and their
file headers, the `icechunk` command-line interface, manifest splitting, the
asynchronous surface, and the storage backends and their settings. All of it is
re-exercised here.

This release **removes and renames a great deal**. Work through
`/app/TEST_INTERFACE.md` §19 before you start editing.

## Read these local references

Normative, under `/app/icechunk-spec/` — start with `README.md`, which navigates
the corpus, then `PROFILE.md`, which records where the corpus and this milestone
disagree and how each disagreement resolves:

* `reference/spec.md` — the on-disk format at this generation: the repo info
  file, the `Ref` and `SnapshotInfo` entries inside it, the file layout, the
  update protocols and the consistency rules.
* `reference/spec-v1.md` — the frozen first-generation format you must keep
  reading and writing, and migrate from.
* `reference/migration-2.md` and `reference/breaking-changes-2.md` — the upgrade
  and the API changes that come with it.
* `guide/understanding-repository-features.md` — the operations log, repository
  status, repository metadata and feature flags.
* `guide/understanding-version-control.md` — amending, empty snapshots and
  anonymous snapshots.
* `guide/guides-moving-nodes.md` and `guide/guides-moving-chunks.md` — moves,
  rearrange sessions, `shift_array` and `reindex_array`.
* `guide/guides-virtual.md` — named virtual chunk containers and `vcc://`.
* `guide/guides-zarr.md` — rectilinear chunk grids.
* `flatbuffers/*.fbs` — the normative binary schemas, including `repo.fbs`.
* `design/010-notes-towards-an-IC-2.md`, `design/011-ref-and-ancestry-entry-point.md`,
  `design/011-IC2-gc-and-expiration-consistency.md`,
  `design/012-some-more-IC2-format-changes.md`, `design/013-feature-flags.md`,
  `design/014-virtual-chunk-ref-efficiency.md`,
  `design/015-extra-data-in-manifests-and-snapshots.md` — motivation, not
  behaviour. `PROFILE.md` records which of their proposals this release actually
  ships.

Also in `/app`:

* **`/app/TEST_INTERFACE.md`** — the public contract this milestone is held to:
  the workspace layout and build commands, which `icechunk::` paths must
  resolve and which must not, every new signature, the error categories, the
  inherited obligations, the full migration list, and a closing section on what
  is **out of scope**.
* **`/app/WORKSPACE_HANDOFF.md`** — what the workspace looks like when you
  arrive and when you hand off, what must be preserved, and what is out of
  bounds.

## Meta requirements

* **Budget.** 7,200 seconds of agent time for this step. Run `bash /app/timer.sh`
  to see how much is left; the configured timeout is authoritative.
* **Cumulative workspace.** `/app` carries forward. Do not delete, rename or
  break anything an earlier milestone established unless
  `/app/TEST_INTERFACE.md` §19 says this release changes it. Keep the crate's
  identity (`icechunk` at `/app/icechunk`, version `2.0.0`) and keep
  `Cargo.lock` resolvable.
* **Network.** This environment has no network access, and everything the build
  needs is already installed. The pinned dependency set in
  `/app/Cargo.lock` is pre-cached in the image and is the whole
  resolvable crate graph; do not add dependencies. Searching for, downloading, copying, wrapping or
  invoking an online implementation of this task, or a task-specific answer, is
  prohibited — as is inspecting `/tests`, later steps, or any solution material.
* **Public surface only.** `icechunk` must work when used from a separate crate
  outside `/app` through its documented public interface, and the `icechunk`
  binary when run as a process. Your tests are not part of the deliverable,
  and your manifests are used as you leave them. Do not write code that
  detects or special-cases its caller.
* **Before handing off,** leave `cargo build` at `/app` and
  `cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk`
  passing — those two are the build contract of `/app/TEST_INTERFACE.md` §1.1 —
  and exercise this milestone's own path once by hand as
  `/app/WORKSPACE_HANDOFF.md` describes. Keeping
  `cargo test -p icechunk --lib` green is good practice for the milestones that
  follow, but your own tests are out of scope (`/app/TEST_INTERFACE.md` §20).
* **Do not edit `/app/icechunk-spec/`.** It is a read-only reference corpus.
