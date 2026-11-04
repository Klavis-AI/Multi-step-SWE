# PROFILE.md — the benchmark's resolutions, milestone 09

The corpus beside this file is the Icechunk project's own documentation, copied
verbatim. It is written for users of the Python package, so it leaves some of
what this benchmark grades open, and occasionally describes something the Rust
crate expresses differently — or something the code does not do at all.

Where this file and the corpus disagree, **this file wins**. Where this file and
`/app/TEST_INTERFACE.md` disagree, **`TEST_INTERFACE.md` wins**: it is the
interface contract, and this file only explains how the corpus maps onto it.

---

## 1. What is new in the corpus at this release

This release reorganised the documentation site, so the file-name map in
`README.md` is the first thing to read — and with it the warning that
`/app/icechunk-spec/` still holds **thirty-four files from earlier milestones
under names this release no longer uses**, including a `guide/spec.md` that is
the *version-1* specification. The table below describes this release's own
documents only.

Ignoring pure renames, the substantive changes are:

| document | status | carries a new normative statement? |
| --- | --- | --- |
| `spec.md` = `reference/spec.md` | **rewritten** — this is now the **version 2** specification | yes, all of it |
| `reference/spec-v1.md` | **new file, old content** — the version 1 specification, frozen | no (it is milestone 08's `spec.md`, retitled, with a cross-reference note added) |
| `reference/migration-2.md` | **new** | yes — the in-place upgrade and its guarantees |
| `reference/breaking-changes-2.md` | **new** | yes, for Python; §4 maps it onto Rust |
| `guide/understanding-repository-features.md` | **new** | yes — status, metadata, ops log, feature flags |
| `guide/guides-moving-nodes.md` | **new** | yes — moves, rearrange sessions, subtree moves, no overwriting |
| `guide/guides-moving-chunks.md` | **new** | yes — chunk-unit offsets, `shift_array`, `reindex_array`, backward functions |
| `guide/guides-zarr.md` | **new** | yes — `## Rectilinear chunk grids` |
| `guide/guides-flaky-networks.md` | **new** (replaces `guide/concurrency.md`) | no — it restates the retry and timeout settings milestone 08 added |
| `guide/related-projects.md` | **new** | no |
| `guide/understanding-version-control.md` | changed | yes — `## Amending a snapshot`, `### Empty Snapshots`, `### Creating Anonymous Snapshots` |
| `guide/guides-virtual.md` | changed | yes — `## Relative Virtual Chunk Containers` |
| `guide/guides-storage.md` | changed | yes — `### HTTP Storage` and `### Redirect Storage` (neither is graded; §6) |
| `flatbuffers/repo.fbs` | **new** | yes — the binary shape of the repo info object |
| `flatbuffers/common.fbs` | renamed from `object_ids.fbs`, extended | yes |
| `flatbuffers/{snapshot,manifest,transaction_log,all}.fbs` | changed | yes |
| `design/011-IC2-gc-and-expiration-consistency.md`, `012`, `013`, `014`, `015` | **new** | no — motivation only |
| `reference/{ops,conflicts,session,snapshots,config,virtual,storage,credentials}.md` | **new** | **no — they are generated stubs; see §2** |

So: **the on-disk format changes at this release**, for the first time since
milestone 06. The version byte in every object header goes from `1` to `2` for
newly written repositories, and the first-generation format stays fully
supported.

## 2. The `reference/` API pages are stubs, not specifications

Eight files under `reference/` are 80-to-170-byte **mkdocstrings include
shims**: a title, one summary sentence, and a `::: icechunk.<module>` directive
the documentation site expands from Python docstrings when it is built. They
carry **no prose and no requirement**:

```
reference/ops.md        reference/conflicts.md   reference/session.md
reference/snapshots.md  reference/config.md      reference/virtual.md
reference/storage.md    reference/credentials.md
```

`reference/icechunk-rust.md` is a short stub of the same kind for the Rust
crate. Do not read a requirement into any of them, and do not treat a name that
appears only in one of their summary lines as a Rust requirement — see §4.

The substantial documents under `reference/` are `spec.md`, `spec-v1.md`,
`migration-2.md`, `breaking-changes-2.md`, plus the two carried over from
milestone 08 (`migration.md`, `version-policy.md`) and `index.md`.

## 3. The resolution this release *reverses*

**R9-0 — `design/011-ref-and-ancestry-entry-point.md` is now implemented.**
Milestone 08's profile said (as §R8-3) that the design document describing a
flatbuffer object at `$ROOT/repo` — holding the spec version, branches, tags,
deleted tags, repository status and every `SnapshotInfo` — was **not**
implemented, that the `repo` key was merely reserved, and that its presence was
to be treated as "written by a newer generation" and refused.

**That resolution is dead.** This is the release that implements the design.
`$ROOT/repo` is now written, read and conditionally updated;
`RepositoryErrorKind::RepositoryIsV2` is **removed**; and a repository with a
`repo` object opens normally. The normative statement of the object's contents
is `reference/spec.md` `#### Repo Info File` and `flatbuffers/repo.fbs`, not the
design document, which is looser and in places wrong about field names. Where
the design document and `spec.md` disagree, `spec.md` wins.

The same applies to `design/010-notes-towards-an-IC-2.md`: its proposals for
`amend`, repository state, distributing dirty sessions, persisting partial
sessions, an efficient `move`, and a directly readable spec version are all
implemented here. Its *rationale* is useful; its sketched signatures are not
normative.

## 4. The corpus documents Python; the crate is Rust

`reference/breaking-changes-2.md` is the densest source of confusion, because
most of what it lists is a Python-only change. Mapping:

**R9-1 — there is no Rust `ForkSession`.** `reference/session.md`'s summary line
names `ForkSession`, and `reference/breaking-changes-2.md` devotes a section to
it. `ForkSession` is a **Python wrapper**. In Rust a fork is an ordinary
`Session` whose `branch()` is `None` and whose `read_only()` is false:
`Session::fork()` creates one, `Session::is_fork()` recognises one. The three
documented rules do hold, as Rust error kinds:
`fork.commit(..).execute()` → `SessionErrorKind::CommitNotAllowed`;
`fork.merge(branch_session)` → `SessionErrorKind::MergeNotAllowed`;
forking a read-only session → `SessionErrorKind::CannotForkReadOnlySession`.
See `TEST_INTERFACE.md` §7.2.

**R9-2 — `upgrade_icechunk_repository` returns a repository; `migrate_1_to_2`
does not.** `reference/migration-2.md` shows
`migrated_repo = ic.upgrade_icechunk_repository(repo, dry_run=False)` and says
the original object is "invalidated" and raises `RuntimeError` if used. The Rust
function is

```rust
migrations::migrate_1_to_2(repo: Repository, dry_run: bool,
                           delete_unused_v1_files: bool,
                           prefetch_concurrency: Option<usize>) -> MigrationResult<()>
```

It **consumes** the handle — invalidation is enforced by the move checker, not by
a runtime error — and returns nothing. Reopen with `Repository::open` to get a
usable handle. The parameter meanings, including `prefetch_concurrency`'s default
of 64, are exactly as the document's table says. See `TEST_INTERFACE.md` §14.

**R9-3 — `Session.commit()` and `Session.flush()` map onto one Rust builder.**
`reference/breaking-changes-2.md` describes keyword-only parameters and a new
`allow_empty`. In Rust there are no keyword arguments: `Session::commit` is no
longer `async`, returns `CommitBuilder<'_>`, and `flush`, `rewrite_manifests` and
`commit_rebasing` are **gone**. The document's `session.flush(msg, metadata=…)`
is `session.commit(msg).anonymous().properties(…).execute().await`, and
`allow_empty=True` is `.allow_empty(true)`. See `TEST_INTERFACE.md` §6.

**R9-4 — the ops-log record is one shape in Python and two orders in Rust.**
`guide/understanding-repository-features.md` says each entry is an `Update` with
three fields, `kind`, `updated_at` and `backup_path`. Rust has no `Update`
struct on this path; it has two tuples, **and their field orders differ**:

| accessor | yields |
| --- | --- |
| `Repository::ops_log()` | `(DateTime<Utc>, UpdateType, Option<String>)` — **time first** |
| `RepoInfo::latest_updates()` | `(UpdateType, DateTime<Utc>, Option<&str>)` — **kind first** |

That is not a typo in this document; both orders are real and both are graded.
`backup_path` is the "internal storage detail" the guide tells Python users to
ignore — it is the file under `overwritten/` that holds the previous batch of
updates, and it is what makes the log walkable past the current object.

**R9-5 — the `UpdateType` variant names differ by a suffix.** The guide writes
`UpdateType.NewCommit`, `UpdateType.BranchCreated`, and so on. The Rust variants
carry an `Update` suffix, except the feature-flag one, which does not:

| guide | Rust |
| --- | --- |
| `NewCommit`, `CommitAmended`, `NewDetachedSnapshot` | `NewCommitUpdate`, `CommitAmendedUpdate`, `NewDetachedSnapshotUpdate` |
| `BranchCreated`, `BranchDeleted`, `BranchReset` | `BranchCreatedUpdate`, `BranchDeletedUpdate`, `BranchResetUpdate` |
| `TagCreated`, `TagDeleted` | `TagCreatedUpdate`, `TagDeletedUpdate` |
| `ConfigChanged`, `MetadataChanged`, `RepoStatusChanged` | `ConfigChangedUpdate`, `MetadataChangedUpdate`, `RepoStatusChangedUpdate` |
| `RepoInitialized`, `RepoMigrated` | `RepoInitializedUpdate`, `RepoMigratedUpdate` |
| `GCRan`, `ExpirationRan` | `GCRanUpdate`, `ExpirationRanUpdate` |
| `FeatureFlagChanged` | `FeatureFlagChanged` — **no suffix** |

Sixteen variants, with the fields `TEST_INTERFACE.md` §5 lists.

**R9-6 — `Repository.list_manifest_files()` is `lookup_manifest_files` in
Rust.** `reference/breaking-changes-2.md` records a Python rename from
`manifest_files()` to `list_manifest_files()`. The Rust method is
`Repository::lookup_manifest_files(&SnapshotId)`. The removal of
`SnapshotInfo::manifests` is real on both sides.

**R9-7 — several breaking changes have no Rust side at all.** The Python 3.12
requirement, the `ChunkType` variant renaming, the `icechunk.*` submodule split,
`inspect_snapshot()` returning a `dict`, the structured `__repr__` strings and
the `list_objects` deprecation are Python-package changes. Nothing in the Rust
crate corresponds to them, and nothing is graded for them. (The *Rust* module
reorganisation at this release is a different thing entirely: the workspace split
of `TEST_INTERFACE.md` §1.1 and §2.)

## 5. Statements whose Rust reading is not the obvious one

**R9-8 — "rearrange session" is asymmetric, and the corpus only says half of
it.** `guide/guides-moving-nodes.md` § "Rearrange Sessions" says node operations
require "a special session type for structural changes only", and that data
modifications and structural reorganisations are therefore distinct commits. Two
consequences, both graded, and the second is not stated anywhere in the corpus:

* a move outside a rearrange session is `SessionErrorKind::NonRearrangeSession`;
* **any** edit inside one — including `shift_array` and `reindex_array`, which
  `guide/guides-moving-chunks.md` presents alongside `move` — is
  `SessionErrorKind::RearrangeSessionOnly`. `shift_array` and `reindex_array`
  are ordinary edits and belong in an ordinary writable session.

**R9-9 — `Session::mode()` reports `Readonly` for a fork.** `SessionMode` is
`{ Readonly, Writable, Rearrange }`, but `mode()` is derived: a session with no
branch name reports `Readonly` regardless of whether it can be written. A fork
has no branch, so `fork.mode() == SessionMode::Readonly` while
`fork.read_only() == false`. Use `is_fork()` and `read_only()` to classify a
fork. `mode()` is graded only for the three sessions `Repository` hands out.

**R9-10 — a fetched `RepoInfo` never changes under you.** Every mutator on
`RepoInfo` returns a new value; none mutates in place. That is what makes the
optimistic-concurrency loop correct, and it is graded directly: an `Arc<RepoInfo>`
fetched before a mutation still shows the old state afterwards.

**R9-11 — "the repo info file is the only mutable object" is about the format,
not about caching.** `reference/spec.md` `### Storage Operations` means every
other object is immutable once written. It does not mean a client must re-read
`repo` on every call; `AssetManager` may cache it. Nothing about caching is
graded.

**R9-12 — the backup path format is stated but not graded.**
`reference/spec.md` `##### Updates to the repo info file` specifies the name of
the copy taken before an overwrite as
`repo.<3000-01-01 minus now, in milliseconds>.<12 random bytes, base32>`.
Implement it; the verifier asserts only that an `overwritten/` prefix appears,
never the name of a file inside it.

**R9-13 — the ops-log chain across several `repo` files is not graded.** The
default bound is 1,000 updates per file
(`DEFAULT_NUM_UPDATES_PER_REPO_INFO_FILE`), and `repo_before_updates()` links to
the previous batch. Implement the chain; the verifier never writes a thousand
updates, so only the single-file case is exercised.

**R9-14 — the retry schedule is not graded.** `reference/spec.md`
`### Consistency and Optimistic Concurrency` says an update that loses the
conditional write is retried, and that an incompatible change fails instead. Only
the second half and the observable serialisation of the first are graded — never
an attempt count, a delay or a backoff constant. Note that
`guide/guides-flaky-networks.md`'s table (`max_tries` 10, `initial_backoff_ms`
100, `max_backoff_ms` 180,000) describes **storage** retries and is accurate;
the repo-info update loop has its own separate defaults
(`max_tries` 100, `initial_backoff_ms` 50, `max_backoff_ms` 30,000) that no
corpus document mentions. They are in `TEST_INTERFACE.md` §4.

**R9-15 — `guide/guides-zarr.md`'s rectilinear example is the format change.**
The document is about Zarr metadata. The Rust-observable change behind it is that
a snapshot now stores `num_chunks` per dimension instead of a chunk length:
`DimensionShape::new(array_length, num_chunks)`. `ArrayShape::new(vec![(20, 10)])`
compiles under milestone 08 *and* here and means different things. See
`TEST_INTERFACE.md` §9 and §19.1.

**R9-16 — container names are unique by replacement, not by rejection.**
`guide/guides-virtual.md` § "Relative Virtual Chunk Containers" says container
names must be unique within a repository.
`RepositoryConfig::set_virtual_chunk_container` enforces that by **replacing**
any existing container with the same name — including its `url_prefix` — rather
than returning an error. That is what makes re-pointing a named container at a
new prefix relocate every `vcc://` reference without rewriting a manifest.
Authorisation still keys on URL prefix, never on name.

**R9-17 — `repo.fbs`'s third availability has no Rust variant.**
`flatbuffers/repo.fbs` declares
`enum RepoAvailability : ubyte { Online = 0, ReadOnly, Offline }`, and
`reference/spec.md` § `##### RepoStatus` follows it. The Rust enum at this
release has exactly the two variants `TEST_INTERFACE.md` §10 names — `Online`
and `ReadOnly` — and `Offline` is reserved in the wire format for a later
release. Nothing is graded either way: no scored case writes an exhaustive
`match` on `RepoAvailability`, so an implementation that mirrors the
FlatBuffers enum with a third variant passes every group.

**R9-18 — the tag tombstone prefix is `tag.`, not `tags.`.**
`reference/spec-v1.md` § `##### Tags` opens by writing a tag's own file as
`refs/tag.$TAG_NAME/ref.json` and closes, in the same section, by writing its
tombstone as `refs/tags.$TAG_NAME/ref.json.deleted` — and
`### Read from Repository` § `#### From Tag` writes `refs/tag.$TAG_NAME/ref.json`
again. The plural spelling is an upstream typo,
carried here verbatim because the corpus is copied unedited. The on-disk prefix
is **`tag.`** for both files — `refs/tag.$TAG_NAME/ref.json.deleted` — which is
what milestone 03's `delete_tag` wrote, what the checked-in first-generation
fixture contains, and what this release's new `refs::list_deleted_tags` and the
first-to-second-generation migration must read. Where `spec-v1.md` and this
resolution disagree, this resolution wins; see `TEST_INTERFACE.md` §14.

## 6. Things the corpus says that are *not* requirements here

* `guide/guides-storage.md` `### HTTP Storage` and `### Redirect Storage`, in
  full, and every cloud backend section (S3, GCS, Azure, Tigris, R2, MinIO).
  The verifier has no network and no object-storage service; only in-memory and
  local-filesystem storage are exercised, and a virtual chunk reference is
  dereferenced only against `file://` URLs inside a temporary directory.
* `guide/guides-flaky-networks.md` as **behaviour**. Retrying on a transport
  error or a stalled stream needs a fault-injecting proxy. Only the
  configuration surface is graded.
* The JavaScript and WebAssembly package. `Changelog.md` announces
  `@earthmover/icechunk` and the corpus links to it; the new `icechunk-js` crate
  in the workspace is **not scored as a working package** — no test runs Node,
  `wasm-pack` or a browser. What is scored is the core refactoring that makes it
  possible: the workspace split and the `compat::IcechunkBoxFuture` shim. See
  `TEST_INTERFACE.md` §20.
* The zstd dictionary for virtual chunk locations
  (`design/014-virtual-chunk-ref-efficiency.md`, `reference/spec.md`
  `##### ChunkRef`). Implement it; nothing measures a compressed manifest.
* The Dask, Xarray and Zarr-client guidance in `guides-dask.md`,
  `guides-xarray.md`, `understanding-parallel.md`, `guides-zarr.md` (outside the
  rectilinear section) and `understanding-faq.md`. All of it is Python-level.
* Every performance number in the corpus. This milestone is not a performance
  task and no throughput figure is graded.

## 7. Resolutions carried forward from earlier milestones

These were settled before this milestone and still apply. They are restated here
because this corpus overlay replaces the earlier ones under the same file names,
and a resolution that is dropped is a resolution that is lost. **Milestone 08's
§R8-3 is deliberately absent: §3 above reverses it.**

**R1-1 — `spec.md` describes the on-disk format, never the Rust API.** It has
lagged the format itself at several checkpoints. The Rust surface is governed by
each milestone's `TEST_INTERFACE.md`.

**R1-2 — `archive/spec-alpha1.md` is historical.** Where it and `spec.md`
disagree, `spec.md` wins.

**R2-1 — the corpus documents the Python package.** A statement in `guide/` or
`reference/` describes `icechunk-python` unless it is plainly about the format.
When the two diverge, the Rust requirement is whatever `TEST_INTERFACE.md`
states. §4 above is this release's instance of the rule.

**R3-1 — the file header is specified only in a design document.** The 39-byte
object header is described in `design/004-binary-format.md`, not in `spec.md`.
Byte 36 is the format generation, and it now takes the value `2` for
newly written objects. A value the library does not support is
`IcechunkFormatErrorKind::InvalidSpecVersion { found, max_supported }`. See
`TEST_INTERFACE.md` §18.3.

**R4-1 — the FlatBuffers file identifier is written but not verified.** Metadata
buffers carry `Ichk` at bytes 4..8. Icechunk writes it and discriminates on the
header's file-type byte instead.

**R5-1 — the command-line interface is undocumented in the corpus.** No guide or
reference document describes the `icechunk` binary. Its grammar, exit statuses
and configuration file — including the fact that repository definitions are
written as YAML `!Variant` tags — are stated in `TEST_INTERFACE.md` §18.4, which
is the sole authority for them.

**R5-2 — chunk accounting reports inline and virtual bytes separately.** Since
milestone 08, `ops::stats::repo_chunks_storage` reports native, virtual and
inline bytes in separate fields. Garbage collection still counts materialised
chunk objects only, since an inline chunk is not an object.

**R6-1 — "format 1.0" did not bump the version byte.** The first-generation byte
value is `1`, and `SpecVersionBin::V1` is that value. This release adds `V2 = 2`
and makes it the default; it does not renumber anything.

**R6-2 — the split-manifest design document is aspirational.**
`design/005-manifest-split.md` describes manifest sets, a maximum manifest size
and bin packing. None of that was implemented. The implemented model is the
chunk-space grid: one manifest per non-empty cell, configured by
`ManifestSplittingConfig`. See `TEST_INTERFACE.md` §18.5.

**R6-3 — there are no default virtual chunk containers.** A repository
authorises virtual chunk access explicitly, by URL prefix, through the third
argument of `Repository::{create, open, open_or_create}`.

**R7-1 — the asynchronous interface has no Rust counterpart.** The `_async`
methods `guide/guides-async.md` documents are PyO3 wrappers. Every I/O method on
the Rust side has always been an `async fn`. What *did* change in Rust at
milestone 08 and still holds: `Storage::can_write` and
`Storage::default_settings` are async and fallible.

**R7-2 — `validate_container` has no default in Rust.** It is a positional,
mandatory `bool` on `Store::set_virtual_ref` and `Store::set_virtual_refs`.

**R7-3 — the cache warnings are logs, not values.** Emit them; their text is not
graded.

**R7-4 — `ManifestFileInfo` accessors are a Python fix.** The Rust struct's
fields — `id`, `size_bytes` and `num_chunk_refs` — have been `pub` throughout,
and still are here: `Repository::lookup_manifest_files` hands back
`ManifestFileInfo` values whose `size_bytes` a caller outside the crate reads
directly. See `TEST_INTERFACE.md` §18.7.

**R7-5 — the virtual-container separator rule.** A container's `url_prefix` must
end in `/`; `VirtualChunkContainer::{new, new_named}` reject one that does not.
The rule is what makes authorisation prefix-exact rather than a substring test.

**R8-1 — the stalled-stream default is ten seconds**, not the sixty
`guide/guides-flaky-networks.md` states (the sentence moved there from
`guide/performance.md` when the site was reorganised). `Some(0)` disables stall
detection. Behaviour is not graded; only the configuration field.

**R8-2 — Azure virtual chunks work**, despite the stale "other storage backends
such as `azure` are on the roadmap" admonition still in
`guide/guides-configuration.md`. Exercising them needs a storage emulator, so
none of it is graded.

**R8-4 — `NO_PROXY` is honoured** alongside the three proxy variables
`guide/guides-configuration.md` lists. Nothing about proxying is graded.

**R8-6 — there is no forward compatibility.** `reference/version-policy.md`: a
library of major version *N* reads and writes spec versions *N* and *N−1*. At
this release that means version 2 **and** version 1, which is exactly why the
first generation stays fully supported here.

**R8-7 — `Session.chunk_type` does not exist in Rust.** The equivalent is
`Session::get_chunk_ref`, whose `Ok(None)` / `Ok(Some(Ref))` /
`Ok(Some(Inline))` / `Ok(Some(Virtual))` are the four documented states.

**R8-8 — `Repository.inspect_snapshot` does not exist in Rust.** The Rust
surface is the free functions in `icechunk::inspect`, which gained
`repo_info_json`, `manifest_json` and `transaction_log_json` here. None of them
is graded.

**R8-9 — `RepositoryConfig::merge` is not new**, and
`virtual_chunk_containers` replaces rather than unions.

**R8-10 — anonymous credentials exist for S3 and GCS only.** `AzureCredentials`
still has no anonymous variant.

**R8-11 — the manifest and snapshot size limits are private.** The graded,
public consequence is `CachingConfig::num_snapshot_nodes()`, default 500,000.

**R8-12 — "a warning for a large number of chunks in a commit" is a log line.**
Emit it; it is not graded.
