# TEST_INTERFACE.md — milestone 09, "Icechunk 2: the repository table of contents"

This is the interface contract for this milestone. It is self-contained: every
name, signature, error category, file path and build command the hidden verifier
uses is either stated here or in a document `instruction.md` points at. Hidden
tests may combine these interfaces in ways this document does not show; they
never reach for one it does not name.

Where this document and `/app/icechunk-spec/` disagree, **this document wins**.
`/app/icechunk-spec/PROFILE.md` records where the two meet.

Everything below is reachable from **outside** the workspace: the verifier
compiles a throw-away crate that declares `icechunk = { path = "/app/icechunk" }`
and uses only `icechunk::` paths, plus the command-line binary. Nothing private,
nothing `#[cfg(test)]`, no sibling crate named directly.

---

## 1. The workspace, the crate, and the build

### 1.1 Membership

`/app` is a Cargo workspace with **nine** members:

```toml
[workspace]
members = [
  "icechunk", "icechunk-types", "icechunk-format", "icechunk-storage",
  "icechunk-s3", "icechunk-arrow-object-store", "icechunk-python",
  "icechunk-macros", "icechunk-js",
]
default-members = ["icechunk"]
resolver = "2"

[workspace.package]
edition = "2024"
rust-version = "1.94.0"
```

`default-members` is unchanged from the previous milestone, so a bare
`cargo build` at `/app` builds **only** the `icechunk` package (and, transitively,
the library crates it depends on). It does not build `icechunk-python`,
`icechunk-js` or `icechunk-macros`.

The verifier's build steps are exactly:

```sh
cd /app && cargo build                                                  # the library
cd /app && cargo build --manifest-path /app/icechunk/Cargo.toml \
                      --features cli --bin icechunk                     # the CLI
```

Both must succeed. A workspace whose root manifest is missing, or whose
`icechunk` package fails to compile, fails every group.

### 1.2 Crate identity

| key | value |
| --- | --- |
| package name | `icechunk` |
| version | `2.0.0` (literal, or `{ workspace = true }` resolving to `2.0.0`) |
| library path | `/app/icechunk` |
| `[[bin]] name` | `icechunk`, `required-features = ["cli"]` |

The publishable sibling crates are all version `2.0.0`:
`icechunk-types`, `icechunk-format`, `icechunk-storage`, `icechunk-s3`,
`icechunk-arrow-object-store`, `icechunk-macros`. `icechunk-python` and
`icechunk-js` are `publish = false`.

### 1.3 Features

```
default = ["s3", "object-store-s3", "object-store-gcs", "object-store-azure",
           "object-store-http", "object-store-fs", "redirect"]
s3, object-store-s3, object-store-gcs, object-store-azure, object-store-http,
object-store-fs, redirect, logs, cli, napi-send-contract, shuttle
```

The verifier builds with default features, plus `cli` for the binary. Anything
gated behind `logs`, `shuttle` or `napi-send-contract` is never exercised.

The `cli` feature gates the **binary**, not the module: `icechunk::cli` must
still be a public module when the feature is off, because the conformance driver
is compiled with default features only and §2.2 requires `icechunk::cli::*` to
resolve there. (Unchanged from milestone 08, whose handoff said the same.)

### 1.4 Reproducible build

`/app/Cargo.lock` is committed and must stay resolvable.

**How the driver crate's dependencies are resolved, and what that asks of you.**
The drivers are always compiled `--locked`, and the verifier never splices one
lockfile into the other side's manifest: it picks one internally consistent
resolution *before* compiling any driver code, by building the dependency graph
alone. It tries, in order:

1. **a task-owned lockfile**, built `--locked --offline`, so nothing is resolved
   and nothing is downloaded;
2. **your own `/app/Cargo.lock`** as the sole seed, completed by a single
   resolver run that may add only the driver crate's own dependencies, then
   frozen with `--locked`;
3. a complete re-resolution of both manifests together, then frozen the same
   way.

**All three are supported, and which one is chosen is out of scope.** The
task-owned lockfile of (1) was resolved from this release's *upstream*
manifests, so it is accepted only where your manifests happen to agree with
them; (2) is the ordinary rescue path for a workspace that changed something.
The verifier's resolution budget is bounded, though, and (2) and (3) both spend
network time inside it, so they are a rescue path rather than a guarantee.

What (2) needs from you is only this: `/app/Cargo.lock` must stay a **regular,
committed file**, and cargo must be the thing that maintains it. Do not edit it
by hand, do not delete it, and do not replace it with a symbolic link — a
missing lockfile, or one that is not a regular file, makes the verifier skip (2)
altogether. A manifest that no resolution can satisfy costs every group at once.

Nothing this milestone checks requires you to add, remove or re-pin a
dependency, so the cheapest thing you can do is leave the manifests as this
release wrote them. Adding a new dependency is fine. **Re-pinning a dependency
that crosses the public API** — `bytes`, `chrono`, `futures`, `serde`,
`serde_json`, `thiserror`, `tokio`, `url`, `flatbuffers`, `zstd` — is what
pushes the verifier off (1); it does not by itself cost a group, but it makes
the driver build depend on a resolution being found at check time.

Dependency versions that cross the boundary at this release: `bytes` 1.11.1,
`chrono` 0.4.44, `futures` 0.3.32, `serde` 1.0.228, `serde_json` 1.0.149,
`thiserror` 2.0.18, `tokio` 1.51.0, `url` 2.5.8, `flatbuffers` 25.12.19,
`zstd` 0.13.3, `object_store` 0.13.2 (reached only through
`icechunk_arrow_object_store::object_store`).

### 1.5 `autotests = false`

`icechunk/Cargo.toml` sets `autotests = false` and `autobenches = false`, and
declares its integration-test targets explicitly. A new file dropped into
`icechunk/tests/` is therefore **not compiled** unless it is added as a `mod` in
`tests/main.rs` or given its own `[[test]]` entry. This does not affect the
verifier — its drivers live outside `/app` with their own `[[test]]` entries —
but it explains why your own new test files may silently never run.

---

## 2. Which `icechunk::` paths must resolve

The library crate is split into six crates, and `icechunk` re-exports them so
that almost every path a milestone-08 caller used still resolves. The verifier
depends on that.

### 2.1 `icechunk/src/lib.rs`

```rust
pub mod asset_manager;  pub mod change_set;  pub mod cli;
pub mod compat;         pub mod config;      pub mod conflicts;
pub mod diff;           pub mod display;     pub mod error;
pub mod feature_flags;
pub use icechunk_format as format;           // the whole crate, re-exported
pub mod inspect;        pub mod migrations;  pub mod ops;
pub mod refs;           pub mod repository;  pub mod session;
pub mod storage;        pub mod store;       pub mod virtual_chunks;

pub use config::{ObjectStoreConfig, RepositoryConfig};
pub use repository::Repository;
#[cfg(feature = "object-store-fs")] pub use storage::new_local_filesystem_storage;
#[cfg(feature = "object-store-s3")] pub use storage::new_s3_object_store_storage;
#[cfg(feature = "s3")]              pub use storage::new_s3_storage;
pub use storage::{ObjectStorage, Storage, StorageError, new_in_memory_storage};
pub use store::Store;
pub use icechunk_types::user_agent;
```

`compat`, `diff`, `display`, `feature_flags` and `migrations` are **new** modules
at this milestone. `icechunk::error` is one line: `pub use icechunk_types::error::ICError;`.

### 2.2 Still resolving, unchanged

- `icechunk::{Repository, RepositoryConfig, ObjectStoreConfig, Storage, StorageError, Store, ObjectStorage}`
- `icechunk::{new_in_memory_storage, new_local_filesystem_storage, new_s3_storage, new_s3_object_store_storage}`
- `icechunk::format::{ByteRange, ChunkId, ChunkIndices, ManifestId, NodeId, ObjectId, Path, PathError, SnapshotId, AttributesId, IcechunkFormatError, IcechunkFormatErrorKind, IcechunkResult, format_constants}`
- `icechunk::format::manifest::{Manifest, ManifestExtents, ManifestRef, ManifestSplits, ChunkPayload, ChunkRef, ChunkInfo, VirtualChunkRef, VirtualChunkLocation, Checksum, SecondsSinceEpoch, VirtualReferenceError, VirtualReferenceErrorKind}`
- `icechunk::format::snapshot::{Snapshot, SnapshotInfo, SnapshotProperties, NodeSnapshot, NodeData, ArrayShape, DimensionShape, DimensionName, ManifestFileInfo}`
- `icechunk::format::{transaction_log::TransactionLog, attributes::*}`
- `icechunk::config::{S3Options, S3Credentials, S3StaticCredentials, S3CredentialsFetcher, GcsCredentials, GcsStaticCredentials, GcsBearerCredential, AzureCredentials, AzureStaticCredentials, Credentials, CachingConfig, CompressionConfig, CompressionAlgorithm, ManifestConfig, ManifestSplittingConfig, ManifestSplitCondition, ManifestSplitDim, ManifestSplitDimCondition, ManifestPreloadConfig, ManifestPreloadCondition, DEFAULT_MAX_CONCURRENT_REQUESTS}`
- `icechunk::storage::{Settings, VersionInfo, ETag, Generation, ListInfo, RetriesSettings, ConcurrencySettings, StorageErrorKind, StorageResult, split_in_multiple_requests, mk_client, new_r2_storage, new_tigris_storage}` — including `VersionInfo::{for_creation, is_create}`, unchanged
- `icechunk::repository::{VersionInfo, RefVersionInfo, RepositoryError, RepositoryErrorKind, RepositoryResult}`
- `icechunk::session::{Session, SessionError, SessionErrorKind, SessionResult, get_chunk, find_coord, is_prefix_match, construct_valid_byte_range}`
- `icechunk::refs::{Ref, RefData, RefError, RefErrorKind, create_tag, update_branch, list_refs, list_tags, list_branches, delete_branch, delete_tag, fetch_tag}`
- `icechunk::ops::gc::{Action, GCConfig, GCSummary, GCError, GCResult, garbage_collect, expire, ExpireResult, ExpiredRefAction}`
- `icechunk::ops::{manifests::rewrite_manifests, stats::*}`
- `icechunk::conflicts::{basic_solver::{BasicConflictSolver, VersionSelection}, detector::ConflictDetector, Conflict, ConflictSolver, ConflictResolution}`
- `icechunk::store::{Store, StoreError, StoreErrorKind, StoreResult}`
- `icechunk::virtual_chunks::{VirtualChunkContainer, VirtualChunkResolver, ChunkFetcher, S3Fetcher, ObjectStoreFetcher}`
- `icechunk::change_set::{ChangeSet, ArrayData}` (the `ChangeSet` *path* resolves; the type is now an enum — §19.6)
- `icechunk::asset_manager::AssetManager`, `icechunk::inspect::*`, `icechunk::cli::*`
- `icechunk::error::ICError`

### 2.3 No longer resolving

| removed path | replacement |
| --- | --- |
| `icechunk::storage::object_store` (module) | items re-exported at `icechunk::storage::{ObjectStorage, ObjectStoreBackend, …}` |
| `icechunk::storage::s3` (module) | items re-exported at `icechunk::storage::{S3Storage, mk_client, …}` (feature `s3`) |
| `icechunk::format::transaction_log::{Diff, DiffBuilder}` | `icechunk::diff::{Diff, DiffBuilder}` |
| `icechunk::format::snapshot::NodeType` | `icechunk::format::NodeType` |
| `icechunk::format::private::Sealed` | not public |
| `icechunk::refs::fetch_branch_tip` | `icechunk::refs::fetch_branch_tip_v1` |
| `icechunk::{session,repository}::raise_if_invalid_snapshot_id` | removed |
| `VirtualChunkLocation::from_absolute_path` | `VirtualChunkLocation::from_url` |
| `ManifestSplits::from_extents` | removed |
| `ICError::{new, with_context}` | `ICError::capture`, `ICError::inject` |
| `format_constants::SpecVersionBin::V0dot1` | `SpecVersionBin::V1` |
| `RepositoryErrorKind::RepositoryIsV2` | removed — a second-generation repository now *opens* (§16) |
| `ops::gc::{expire_ref, ExpireRefResult}` | `ops::expiration_v1::{expire_ref, ExpireRefResult}` |

### 2.4 New paths

```
icechunk::format::repo_info::{RepoInfo, RepoStatus, RepoAvailability, UpdateType, UpdateInfo}
icechunk::format::{NodeType, Move,
                   CONFIG_FILE_PATH, REPO_INFO_FILE_PATH, CHUNKS_FILE_PATH,
                   MANIFESTS_FILE_PATH, SNAPSHOTS_FILE_PATH,
                   TRANSACTION_LOGS_FILE_PATH, OVERWRITTEN_FILES_PATH,
                   V1_REFS_FILE_PATH}
icechunk::diff::{Diff, DiffBuilder}
icechunk::display::{AncestryGraph, ancestry_graph::{AncestryNode, LayoutElement,
                    palette_ansi, palette_hex}}
icechunk::feature_flags::{FeatureFlag, MOVE_NODE_FLAG, CREATE_TAG_FLAG,
                          DELETE_TAG_FLAG, find_feature_flag_id,
                          raise_if_feature_flag_disabled}
icechunk::migrations::{migrate_1_to_2, MigrationError, MigrationErrorKind, MigrationResult}
icechunk::compat::IcechunkBoxFuture
icechunk::session::{SessionMode, CommitBuilder, CommitMethod, RebaseHook,
                    ReindexMapping, ReindexOperationResult}
icechunk::repository::DetectedSpecVersion
icechunk::store::ArrayMetadata
icechunk::storage::{TimeoutSettings, StorageInfo, GetModifiedResult,
                    VersionedUpdateResult, DeleteObjectsResult,
                    s3_config, latency, redirect, new_redirect_storage,
                    new_http_storage, split_in_multiple_equal_requests, strip_quotes}
icechunk::user_agent
```

The eight path constants have exactly these values:
`"config.yaml"`, `"repo"`, `"chunks"`, `"manifests"`, `"snapshots"`,
`"transactions"`, `"overwritten"`, `"refs"`.

`user_agent()` returns a `&'static str` beginning `"icechunk-rust-"`.

`s3_config` is a module, and `storage::s3_config::S3Options` is the same type
`config::S3Options` names in §2.2 — the canonical path moved, the re-export did
not.

Four of the free functions above are *called* by the verifier rather than only
named, so their signatures and their observable behaviour are fixed:

```rust
// icechunk::storage
pub fn strip_quotes(s: &str) -> &str;
// Removes one `"` from each end when BOTH are present and returns the argument
// unchanged otherwise: `strip_quotes("\"quoted\"") == "quoted"`,
// `strip_quotes("bare") == "bare"`.

pub fn split_in_multiple_equal_requests(
    range: &std::ops::Range<u64>, ideal_req_size: u64, max_requests: u16,
) -> impl Iterator<Item = std::ops::Range<u64>>;
// Same parameter order and meaning as `split_in_multiple_requests` (§2.2), which
// is unchanged at this release: it yields
// `max(1, min(len.div_ceil(ideal_req_size), max_requests))` contiguous ranges
// covering `range`, so `split_in_multiple_equal_requests(&(0..8), 4, 8)` yields
// exactly two. Only the count is in scope, not how the two differ from
// `split_in_multiple_requests`' split.

// icechunk::display::ancestry_graph
pub fn palette_ansi(column: usize) -> &'static str;
pub fn palette_hex(column: usize) -> &'static str;
// Both take a column index modulo the palette length, so every `usize` is
// accepted and neither panics or returns an owned `String`. The colour values
// themselves are out of scope (§20).
```

---

## 3. The repository table of contents

The single mutable object of a second-generation repository lives at
`$ROOT/repo`. Its in-memory form is `icechunk::format::repo_info::RepoInfo`,
reached through the asset manager:

```rust
impl AssetManager {
    pub async fn fetch_repo_info(&self)
        -> RepositoryResult<(Arc<RepoInfo>, icechunk::storage::VersionInfo)>;
    pub async fn fetch_repo_info_backup(&self, file_name: &str)
        -> RepositoryResult<(Arc<RepoInfo>, icechunk::storage::VersionInfo)>;
    pub async fn update_repo_info(
        &self,
        retry_settings: &icechunk::storage::RetriesSettings,
        update: impl FnMut(Arc<RepoInfo>, &str, icechunk::storage::VersionInfo)
                    -> RepositoryResult<Arc<RepoInfo>>,
    ) -> RepositoryResult<icechunk::storage::VersionInfo>;
    pub fn fail_unless_spec_at_least(&self, minimum: SpecVersionBin) -> RepositoryResult<()>;
    pub fn spec_version(&self) -> SpecVersionBin;
    pub fn backup_path_for_repo_info(&self) -> String;
    pub async fn can_write_to_storage(&self) -> RepositoryResult<bool>;
    pub async fn list_chunks(&self)       -> RepositoryResult<impl Stream<Item = …>>;
    pub async fn list_manifests(&self)    -> RepositoryResult<impl Stream<Item = …>>;
    pub async fn list_snapshots(&self)    -> RepositoryResult<impl Stream<Item = …>>;
    pub async fn list_transaction_logs(&self) -> RepositoryResult<impl Stream<Item = …>>;
}
```

`Repository::asset_manager(&self) -> &Arc<AssetManager>` is the entry point.

### 3.1 Read accessors

All take `&self` and return `IcechunkResult<…>`:

```rust
impl RepoInfo {
    pub fn spec_version(&self)   -> IcechunkResult<SpecVersionBin>;
    pub fn branch_names(&self)   -> IcechunkResult<impl Iterator<Item = &str>>;
    pub fn branches(&self)       -> IcechunkResult<impl Iterator<Item = (&str, SnapshotId)>>;
    pub fn tag_names(&self)      -> IcechunkResult<impl Iterator<Item = &str>>;
    pub fn tags(&self)           -> IcechunkResult<impl Iterator<Item = (&str, SnapshotId)>>;
    pub fn deleted_tags(&self)   -> IcechunkResult<impl Iterator<Item = &str>>;
    pub fn tag_was_deleted(&self, name: &str) -> IcechunkResult<bool>;
    pub fn resolve_branch(&self, name: &str)  -> IcechunkResult<SnapshotId>;
    pub fn resolve_tag(&self, name: &str)     -> IcechunkResult<SnapshotId>;
    pub fn all_snapshots(&self)
        -> IcechunkResult<impl Iterator<Item = IcechunkResult<SnapshotInfo>>>;
    pub fn ancestry<'a>(&'a self, snapshot: &SnapshotId)
        -> IcechunkResult<impl Iterator<Item = IcechunkResult<SnapshotInfo>> + Send + use<'a>>;
    pub fn find_snapshot(&self, id: &SnapshotId) -> IcechunkResult<SnapshotInfo>;
    pub fn config<C: serde::de::DeserializeOwned>(&self) -> IcechunkResult<Option<C>>;
    pub fn config_bytes_raw(&self) -> IcechunkResult<Option<Vec<u8>>>;
    pub fn metadata(&self) -> IcechunkResult<SnapshotProperties>;
    pub fn status(&self)   -> IcechunkResult<RepoStatus>;
    pub fn enabled_feature_flags(&self)
        -> IcechunkResult<Option<impl DoubleEndedIterator<Item = u16> + ExactSizeIterator>>;
    pub fn disabled_feature_flags(&self) -> IcechunkResult<Option<…same…>>;
    pub fn feature_flag_enabled(&self, id: u16) -> IcechunkResult<Option<bool>>;
    pub fn latest_updates(&self)
        -> IcechunkResult<impl Iterator<Item =
               IcechunkResult<(UpdateType, DateTime<Utc>, Option<&str>)>>>;
    pub fn repo_before_updates(&self) -> IcechunkResult<Option<&str>>;
    pub fn bytes(&self) -> &[u8];
    pub fn from_buffer(buffer: Vec<u8>) -> IcechunkResult<RepoInfo>;
}
```

What the verifier asserts about content:

- `branch_names`/`tag_names` name **every** live branch and tag, and agree with
  `Repository::{list_branches, list_tags}`, which return `BTreeSet<String>` and
  are therefore in name order (§18.1). `resolve_branch`/`resolve_tag` give
  the snapshot each points at.
- `all_snapshots` names every snapshot the repository holds, including anonymous
  ones; `find_snapshot` returns a `SnapshotInfo` whose `message` and `metadata`
  are the ones the commit carried.
- `ancestry(&id)` walks parents newest-first without reading any snapshot object;
  the initial snapshot has `parent_id == None`.
- `deleted_tags`/`tag_was_deleted` remember a deleted tag's name forever, and
  creating a tag with that name again must fail.
- `config::<RepositoryConfig>()` returns the configuration `save_config()` last
  wrote, or `None` if none was ever written.
- `metadata()` returns the repository-level metadata of §12.
- `status()` returns §10's `RepoStatus`.
- `enabled_feature_flags`/`disabled_feature_flags` list only flags that are
  **not** in their default state; both are `None` or empty on a fresh
  repository, and `feature_flag_enabled(id)` is `None` for a default flag.
- `from_buffer(toc.bytes().to_vec())` reconstructs an equivalent view.

### 3.2 Mutators are pure

Every mutator takes `&self` and returns a **new** `RepoInfo`; none mutates in
place, and none touches storage — writing the result back is `update_repo_info`'s
job (§4).

```rust
impl RepoInfo {
    pub fn add_snapshot(&self, spec_version: SpecVersionBin, snap: SnapshotInfo,
                        branch: Option<&str>, update_type: UpdateType,
                        update_time: Option<DateTime<Utc>>,
                        previous_file: &str, num_updates_per_file: u16)
        -> IcechunkResult<Self>;
    pub fn add_branch(&self, spec_version: SpecVersionBin, name: &str,
                      snap: &SnapshotId, previous_file: &str,
                      num_updates_per_file: u16) -> IcechunkResult<Self>;
    pub fn update_branch(&self, spec_version: SpecVersionBin, name: &str,
                         new_snap: &SnapshotId, previous_file: &str,
                         num_updates_per_file: u16) -> IcechunkResult<Self>;
    pub fn delete_branch(&self, spec_version: SpecVersionBin, name: &str,
                         previous_file: &str, num_updates_per_file: u16)
        -> IcechunkResult<Self>;
    pub fn add_tag(&self, spec_version: SpecVersionBin, name: &str,
                   snap: &SnapshotId, previous_file: &str,
                   num_updates_per_file: u16) -> IcechunkResult<Self>;
    pub fn delete_tag(&self, spec_version: SpecVersionBin, name: &str,
                      previous_file: &str, num_updates_per_file: u16)
        -> IcechunkResult<Self>;
    pub fn set_metadata(&self, spec_version: SpecVersionBin,
                        metadata: &SnapshotProperties, previous_file: &str,
                        num_updates_per_file: u16) -> IcechunkResult<Self>;
    pub fn set_config<C: serde::Serialize>(&self, spec_version: SpecVersionBin,
                                           config: &C, previous_file: &str,
                                           num_updates_per_file: u16)
        -> IcechunkResult<Self>;
    pub fn set_status(&self, spec_version: SpecVersionBin, status: &RepoStatus,
                      update_config: Option<(&str, u16)>) -> IcechunkResult<Self>;
    pub fn update_feature_flag(&self, spec_version: SpecVersionBin, flag_id: u16,
                               enabled: Option<bool>, previous_file: &str,
                               num_updates_per_file: u16) -> IcechunkResult<Self>;
}
```

`previous_file` names the `repo` backup the new operations-log entry points back
at, and `num_updates_per_file` is §5's `num_updates_per_repo_info_file()`. Both
are recorded in the returned `RepoInfo`; neither is in scope for its value, and
the verifier passes a literal for each.

An `Arc<RepoInfo>` already in hand therefore keeps showing the state it was
fetched at, both after a mutator is called **on** it and after another mutation
lands in storage. The verifier asserts both: it calls `add_branch`, `add_tag` and
`set_metadata` directly on a fetched table of contents, checks the returned value
carries the change and the receiver does not, and checks the stored
`storage::VersionInfo` is unmoved afterwards. It also asserts that when a
mutation *does* land, the `storage::VersionInfo` returned alongside — and the
serialised `RepoInfo::bytes()` — differ before and after.

### 3.3 On-disk layout

A second-generation repository holds `repo`, `snapshots/`, `manifests/`,
`transactions/`, `chunks/` and `overwritten/`, and **no** `refs/` prefix and
**no** `config.yaml`. Before the `repo` object is overwritten, the previous copy
is copied under `overwritten/`. The verifier asserts the presence of `repo`,
`snapshots/`, `manifests/`, `transactions/`, `chunks/` and `overwritten/`, and
the absence of `refs/` and `config.yaml`, on a filesystem-backed repository.
It does not assert the naming scheme of a file under `overwritten/`.

The first snapshot's id is the well-known `Snapshot::INITIAL_SNAPSHOT_ID` and
its message is `Snapshot::INITIAL_COMMIT_MESSAGE` (`"Repository initialized"`).

---

## 4. Optimistic concurrency

Updating the table of contents is: fetch it, apply a pure change in memory,
conditionally write it back; on a losing conditional write, re-read and retry.
The retry loop lives in `AssetManager::update_repo_info`: five immediate retries
with no delay, then jittered exponential backoff bounded by
`RepoUpdateRetryConfig` (`max_tries = 100`, `initial_backoff_ms = 50`,
`max_backoff_ms = 30_000`). Exhaustion is
`RepositoryErrorKind::RepoUpdateAttemptsLimit(u64)`.

**The schedule itself is out of scope** — no test counts attempts, measures a
delay, or asserts a backoff constant. What is in scope is the observable
serialisation:

- Several handles on one repository mutating the table of contents concurrently
  (four `create_branch` calls, or two commits on two branches) must **all**
  succeed, and the resulting table of contents must name every change exactly
  once.
- An *incompatible* change fails instead of retrying, and changes nothing:
  `reset_branch(branch, to, Some(from))` where the branch is not at `from`
  yields `RepositoryErrorKind::Conflict { expected_parent, actual_parent }`;
  resetting a branch that no longer exists is an error.

`RepositoryErrorKind::RepoInfoUpdated` exists for the "someone else wrote in the
interim" signal; it is not asserted directly.

---

## 5. The operations log

```rust
impl Repository {
    pub async fn ops_log(&self) -> RepositoryResult<(
        impl Stream<Item = RepositoryResult<(DateTime<Utc>, UpdateType, Option<String>)>>
             + Send + use<>,
        Arc<RepoInfo>,
        icechunk::storage::VersionInfo,
    )>;
}
```

Note the tuple order carefully: `ops_log` yields **`(time, kind, backup_path)`**,
while `RepoInfo::latest_updates` yields **`(kind, time, backup_path)`**. Both
orders are asserted.

Entries are **newest first**, with strictly decreasing timestamps, and cover the
repository's whole lifetime: when the current `repo` object's bounded update list
runs out, `repo_before_updates()` names the backup under `overwritten/` that
holds the previous batch, and the stream follows the chain. The bound is
`RepositoryConfig::num_updates_per_repo_info_file()`, default
`DEFAULT_NUM_UPDATES_PER_REPO_INFO_FILE = 1_000`.

`UpdateType` has exactly these sixteen variants:

```rust
pub enum UpdateType {
    RepoInitializedUpdate,
    RepoMigratedUpdate   { from_version: SpecVersionBin, to_version: SpecVersionBin },
    RepoStatusChangedUpdate { status: RepoStatus },
    ConfigChangedUpdate,
    MetadataChangedUpdate,
    TagCreatedUpdate     { name: String },
    TagDeletedUpdate     { name: String, previous_snap_id: SnapshotId },
    BranchCreatedUpdate  { name: String },
    BranchDeletedUpdate  { name: String, previous_snap_id: SnapshotId },
    BranchResetUpdate    { name: String, previous_snap_id: SnapshotId },
    NewCommitUpdate      { branch: String, new_snap_id: SnapshotId },
    CommitAmendedUpdate  { branch: String, previous_snap_id: SnapshotId, new_snap_id: SnapshotId },
    NewDetachedSnapshotUpdate { new_snap_id: SnapshotId },
    GCRanUpdate,
    ExpirationRanUpdate,
    FeatureFlagChanged   { id: u16, new_value: Option<bool> },
}
```

`UpdateType` derives `PartialEq`, and the verifier matches on variants and their
fields. Which operation produces which entry:

| operation | entry |
| --- | --- |
| `Repository::create` | `RepoInitializedUpdate` |
| `Session::commit(..).execute()` | `NewCommitUpdate { branch, new_snap_id }` |
| `… .amend().execute()` | `CommitAmendedUpdate { branch, previous_snap_id, new_snap_id }` |
| `… .anonymous().execute()` | `NewDetachedSnapshotUpdate { new_snap_id }` |
| `create_branch` / `delete_branch` / `reset_branch` | `BranchCreatedUpdate` / `BranchDeletedUpdate` / `BranchResetUpdate` |
| `create_tag` / `delete_tag` | `TagCreatedUpdate` / `TagDeletedUpdate` |
| `save_config` | `ConfigChangedUpdate` |
| `set_metadata` / `update_metadata` | `MetadataChangedUpdate` |
| `set_status` | `RepoStatusChangedUpdate { status }` |
| `set_feature_flag` | `FeatureFlagChanged { id, new_value }` |
| `ops::gc::garbage_collect` | `GCRanUpdate` |
| `ops::gc::expire` | `ExpirationRanUpdate` |
| `migrations::migrate_1_to_2` | `RepoMigratedUpdate { from_version, to_version }` |

Exactly one entry per mutation. A non-commit operation must appear in the log —
that is the point of the log, since `ancestry` can never show one.

---

## 6. `Session::commit` is a builder

```rust
impl Session {
    pub fn commit(&mut self, message: impl Into<String>) -> CommitBuilder<'_>;   // NOT async
}

impl<'a> CommitBuilder<'a> {
    pub fn properties(self, properties: SnapshotProperties) -> Self;
    pub fn max_concurrent_nodes(self, n: usize) -> Self;   // parallel array flush; default 1
    pub fn allow_empty(self, allow_empty: bool) -> Self;
    pub fn amend(self) -> Self;
    pub fn anonymous(self) -> Self;
    pub fn rewrite_manifests(self) -> Self;
    pub fn rebase(self, solver: &'a (dyn ConflictSolver + Send + Sync), attempts: u16) -> Self;
    pub fn before_rebase_hook(self, hook: RebaseHook) -> Self;
    pub fn after_rebase_hook(self, hook: RebaseHook) -> Self;
    pub async fn execute(self) -> SessionResult<SnapshotId>;
}

pub type RebaseHook =
    Box<dyn Fn(u16) -> Pin<Box<dyn Future<Output = ()> + Send>> + Send + Sync>;
pub enum CommitMethod { NewCommit, Amend }
```

`Session::flush`, `Session::rewrite_manifests` and `Session::commit_rebasing` are
**gone**; the builder replaces all three.

Semantics the verifier asserts:

- `commit(msg).execute().await?` moves the branch and returns the new snapshot
  id; the session is clean afterwards.
- Committing a session with no changes is `SessionErrorKind::NoChangesToCommit`
  unless `allow_empty(true)` is set.
- `properties(p)` is what `SnapshotInfo::metadata` reports afterwards.
- `max_concurrent_nodes(n)` flushes array nodes in parallel; every chunk of every
  array must still read back. No timing is measured.
- `rewrite_manifests()` rewrites the manifests and produces a new snapshot
  without changing any chunk.

Illegal combinations produce
`SessionErrorKind::InvalidCommitConfiguration { reason: &'static str }` with
these exact reasons (the reason string *is* part of the contract, because it is
the only thing distinguishing the cases):

| combination | reason |
| --- | --- |
| `.anonymous().amend()` | `"anonymous commits cannot be amended"` |
| `.anonymous().rebase(..)` | `"anonymous commits cannot use rebase"` |
| `.rewrite_manifests().rebase(..)` | `"rewrite_manifests cannot be combined with rebase"` |
| a rebase hook with no `.rebase(..)` | `"rebase hooks require .rebase() to be set"` |

### 6.1 `amend`

`amend()` replaces the branch's previous commit instead of adding one. After it:

- the branch tip is a **new** snapshot id, and the amended-away id no longer
  appears in `Repository::ancestry`;
- both the amended-away changes and the amending changes are present in the tip;
- an `allow_empty(true)` amend with no data changes edits only the message;
- the log entry is `CommitAmendedUpdate`.

Amending the repository's first commit is refused. The error is
`SessionErrorKind::NoAmendForInitialCommit`, whose `Display` contains the words
`"first commit"`; the verifier checks the substring rather than the whole
sentence. Amending on a first-generation repository is
`RepositoryErrorKind::BadRepoVersion` wrapped in
`SessionErrorKind::RepositoryError` (§16).

---

## 7. Anonymous snapshots and forks

### 7.1 Anonymous snapshots

`commit(msg).anonymous().execute()` writes a snapshot no branch and no tag
points at, and detaches the session: afterwards `branch()` is `None`,
`has_uncommitted_changes()` is false, and `snapshot_id()` is the new id. The
snapshot is first-class: it appears in `RepoInfo::all_snapshots`, in
`find_snapshot`, and in the operations log as `NewDetachedSnapshotUpdate`. It is
readable with `VersionInfo::SnapshotId(..)` and adoptable with
`reset_branch(branch, &anon, Some(&current_tip))`.

### 7.2 Forks

**There is no Rust type named `ForkSession`.** `reference/session.md` in the
corpus names one; that is the Python wrapper. In Rust a fork is a `Session`
whose `branch()` is `None` and which is not read-only.

```rust
impl Session {
    pub async fn fork(&self) -> SessionResult<Self>;
    pub fn is_fork(&self) -> bool;          // branch_name.is_none() && !read_only()
    pub fn branch(&self) -> Option<&str>;
    pub fn read_only(&self) -> bool;
    pub fn mode(&self) -> SessionMode;
    pub fn has_uncommitted_changes(&self) -> bool;
    pub fn as_bytes(&self) -> SessionResult<Vec<u8>>;
    pub fn from_bytes(bytes: &[u8]) -> SessionResult<Self>;    // was Vec<u8>
    pub async fn merge(&mut self, other: Session) -> SessionResult<()>;
    pub fn discard_changes(&mut self) -> SessionResult<()>;    // no longer returns the change set
    pub async fn status(&self) -> SessionResult<Diff>;
    pub async fn rebase(&mut self, solver: &(dyn ConflictSolver + Send + Sync)) -> SessionResult<()>;
}
```

`fork()` flushes an **anonymous snapshot** of the session it is called on —
uncommitted changes included, and with no prior commit required — and returns a
writable, branch-less session based on it. `Session` is `Clone`, so a fork can be
handed to several workers.

Lifecycle rules, all in scope:

| attempt | result |
| --- | --- |
| `read_only_session.fork()` | `SessionErrorKind::CannotForkReadOnlySession` |
| `fork.commit(..).execute()` | `SessionErrorKind::CommitNotAllowed` |
| `fork.commit(..).anonymous().execute()` | allowed |
| `fork.merge(branch_session)` | `SessionErrorKind::MergeNotAllowed` |
| `fork.merge(another_fork)` | allowed — this is how workers are reduced before the base merges the result |
| `branch_session.merge(fork)` | the supported direction |

The rule is a **direction**, not a prohibition on forks. Of the four
branch-ness combinations the direction rule rejects exactly one — a branch-less
receiver absorbing a branch-bound argument — and a fork merging another fork is
therefore allowed. That is how a fan-out is reduced, and the verifier does
exactly that before merging the reduced worker into the base session.
(A read-only receiver is refused separately, with
`SessionErrorKind::ReadOnlySession`.)

`SessionMode` is `{ Readonly, Writable, Rearrange }`, derives `Debug`, `Clone`,
`Copy`, `PartialEq` and `Eq`, and `Session::mode()` is
derived from two things: a session with no branch name reports `Readonly`, and
otherwise the mode follows the change-set kind. **A fork therefore reports
`SessionMode::Readonly` even though `read_only()` is false** — use `is_fork()`
and `read_only()`, not `mode()`, to classify a fork. `mode()` is asserted only
for the three sessions `Repository` hands out:
`readonly_session` → `Readonly`, `writable_session` → `Writable`,
`rearrange_session` → `Rearrange`.

**Serialisation caveat.** `Session::as_bytes`/`from_bytes` carry the session's
*storage configuration*, not the bytes behind it. An in-memory backend
deserialises as a fresh, empty store, so a worker rebuilt from bytes can only
reach a repository whose storage is addressable by configuration — a filesystem
or object-store backend. Hidden tests that round-trip a session through bytes use
a filesystem-backed repository for this reason; tests that merely clone a fork
use in-memory storage.

The property the verifier checks is behavioural: **a base session that merges N
forked workers must end up equal to one session that made the same writes in
sequence** — same chunk bytes at every coordinate and the same node list.

---

## 8. Metadata-only relabelling

### 8.1 `move_node` needs a rearrange session

```rust
impl Repository {
    pub async fn rearrange_session(&self, branch: &str) -> RepositoryResult<Session>;
}
impl Session {
    pub async fn move_node(&mut self, from: Path, to: Path) -> SessionResult<()>;
}
```

The split is asymmetric and exact:

- `move_node` on a session that is not a rearrange session →
  `SessionErrorKind::NonRearrangeSession`.
- **Any edit** on a rearrange session — `add_group`, `add_array`, `update_group`,
  `update_array`, `delete_group`, `delete_array`, `delete_node`, `set_chunk_ref`,
  `delete_chunks`, `reindex_array`, `shift_array`, `merge` →
  `SessionErrorKind::RearrangeSessionOnly`.
- `move_node` refuses to overwrite an existing node:
  `SessionErrorKind::MoveWontOverwrite(String)`.
- Moving a group moves every descendant with it.
- A move copies **no chunk bytes**: the chunk objects on storage are unchanged,
  and reading the moved array at the same coordinates returns the same bytes.
- `rearrange_session` additionally requires the second on-disk generation
  (`RepositoryErrorKind::BadRepoVersion`), an `Online` status (§10), and the
  `move_node` feature flag (§11).

A committed move appears in `Repository::diff` as `Diff::moved_nodes`, one
`icechunk::format::Move { from, to, node_id, node_type }` per relabelled node
(so moving a group with one array yields two). `Diff` keeps the shape earlier
milestones gave it, with one new field:

```rust
pub struct Diff {                                   // icechunk::diff
    pub new_groups:      BTreeSet<Path>,
    pub new_arrays:      BTreeSet<Path>,
    pub deleted_groups:  BTreeSet<Path>,
    pub deleted_arrays:  BTreeSet<Path>,
    pub updated_groups:  BTreeSet<Path>,
    pub updated_arrays:  BTreeSet<Path>,
    pub updated_chunks:  BTreeMap<Path, BTreeSet<ChunkIndices>>,
    pub moved_nodes:     Vec<Move>,                 // NEW
}
impl Diff { pub fn is_empty(&self) -> bool; }
```

A move must leave `new_arrays`, `deleted_arrays` and `updated_chunks` empty: it
is a relabelling, not a write.

### 8.2 `shift_array` and `reindex_array` are ordinary edits

```rust
pub type ReindexOperationResult = Result<Option<ChunkIndices>, SessionError>;
pub enum ReindexMapping<'a> {
    ForwardOnly(Box<dyn Fn(&ChunkIndices) -> ReindexOperationResult + 'a>),
    ForwardBackward {
        forward:  Box<dyn Fn(&ChunkIndices) -> ReindexOperationResult + 'a>,
        backward: Box<dyn Fn(&ChunkIndices) -> ReindexOperationResult + 'a>,
    },
}
impl Session {
    pub async fn reindex_array<'a>(&mut self, array_path: &Path,
                                   calculate_new_index: ReindexMapping<'a>) -> SessionResult<()>;
    pub async fn shift_array(&mut self, array_path: &Path, chunk_offset: &[i64])
        -> SessionResult<()>;
}
```

They run in an **ordinary `writable_session`**, not a rearrange session; calling
either on a rearrange session is `RearrangeSessionOnly`.

- Offsets are **chunk** indices, never element indices. A one-chunk shift moves a
  whole chunk's payload from position *i* to position *i ± 1*.
- `shift_array` discards chunks that fall outside the array and clears vacated
  source positions to the fill value (`get_chunk_ref` returns `Ok(None)` there).
  It is defined as `reindex_array` with a `ForwardBackward` mapping built from
  the offset and the array's `num_chunks()`.
- `reindex_array` visits only non-empty chunk positions. `forward` returning
  `Ok(None)` leaves that chunk where it is; returning an out-of-range coordinate
  is `SessionErrorKind::InvalidIndex { coords, path }`.
- With `ForwardOnly`, a source position that is not also a destination keeps its
  old chunk (stale data). With `ForwardBackward`, `backward` is used to clear the
  positions that should now be empty.

---

## 9. Rectilinear chunk grids

There is no `ChunkGrid` Rust enum. What changed is the stored shape: per
dimension Icechunk now records **`num_chunks`**, not a chunk length.

```rust
impl DimensionShape {
    pub fn new(array_length: u64, num_chunks: u32) -> Self;   // was (u64, NonZeroU64)
    pub fn array_length(&self) -> u64;
    pub fn num_chunks(&self) -> u32;                          // replaces chunk_length()
}
impl ArrayShape {
    pub fn new<I: IntoIterator<Item = (u64, u32)>>(it: I) -> Option<Self>;   // was (u64, u64)
    pub fn num_chunks(&self) -> impl Iterator<Item = u32>;
    pub fn valid_chunk_coord(&self, coord: &ChunkIndices) -> bool;
    pub fn get(&self, ax: usize) -> Option<DimensionShape>;
    pub fn iter(&self) -> impl Iterator<Item = &DimensionShape>;
    pub fn len(&self) -> usize;
    pub fn is_empty(&self) -> bool;
}
```

The grid itself is parsed out of the Zarr `zarr.json` document:

```rust
pub struct ArrayMetadata {           // icechunk::store
    pub shape: Vec<u64>,
    pub dimension_names: Option<Vec<Option<String>>>,
    /* private: node_type, chunk_grid */
}
impl ArrayMetadata {
    pub fn get_chunk_shapes<'a>(&self, coords: impl Iterator<Item = &'a ChunkIndices> + 'a)
        -> StoreResult<Box<dyn Iterator<Item = StoreResult<Vec<u32>>> + 'a>>;
}
```

`ArrayMetadata` is `serde::Deserialize`, so a `zarr.json` document can be parsed
directly. Accepted `chunk_grid.name` values:

- `"regular"`, configuration key `chunk_shape`: one size per dimension;
- `"rectilinear"`, configuration key `chunk_shapes`: per dimension either a
  literal list of sizes (`[2,2,2]`), a run-length pair list (`[[2,3]]` = "2,
  three times"), or a mix (`[1,[2,1],3]`). The `configuration` also carries
  `"kind": "inline"`.

Anything else is `StoreErrorKind::BadChunkGridMetadata(String)`. Only the error
category is in scope; the payload's wording is not asserted at all, here or
anywhere else (§20).

`get_chunk_shapes` yields one `Vec<u32>` per requested coordinate, giving that
chunk's size along each dimension, and yields an `Err` item for a coordinate
outside the grid.

`ArrayShape::valid_chunk_coord` is the grid's boundary, and the boundary is
enforced on chunk access as well as on relabelling: writing a chunk at a
coordinate it rejects — `Session::set_chunk_ref` — is
`SessionErrorKind::InvalidIndex { coords, path }`, the same error
`reindex_array`'s forward function raises in §8.2. Because a dimension now
records `num_chunks`, that boundary **moved** since milestone 08: with
`ArrayShape::new(vec![(20, 10)])` chunk index 9 is now the last *valid* index
and 10 the first invalid one, where at milestone 08 the same call meant two
chunks and 9 was already out of range. See §19.1.

---

## 10. Repository status

```rust
pub enum RepoAvailability { Online, ReadOnly }
pub struct RepoStatus {
    pub availability: RepoAvailability,
    pub set_at: DateTime<Utc>,
    pub limited_availability_reason: Option<String>,
}
impl RepoStatus { pub fn error_msg(&self) -> String; }
// `RepoAvailability` derives Debug, Clone, Copy, PartialEq, Eq;
// `RepoStatus` derives Debug, Clone, PartialEq, Eq. `RepoStatus: PartialEq` is
// what makes §5's "`UpdateType` derives `PartialEq`" possible, because
// `RepoStatusChangedUpdate` carries one.

impl Repository {
    pub async fn get_status(&self) -> RepositoryResult<RepoStatus>;
    pub async fn set_status(&self, status: &RepoStatus) -> RepositoryResult<()>;
}
```

Exactly three fields. A fresh repository is `Online` with no reason. While a
repository is `ReadOnly`:

- `writable_session` fails with `RepositoryErrorKind::ReadonlyRepository(String)`,
  whose payload includes `RepoStatus::error_msg()` (which contains the reason);
- `rearrange_session` fails;
- `readonly_session` and `ancestry` keep working;
- setting the status back to `Online` restores writes.

`set_status` works in both directions regardless of the current status. Each call
adds a `RepoStatusChangedUpdate { status }` entry to the operations log.

Garbage collection and expiration also refuse to run on a non-`Online`
second-generation repository, with `RepositoryErrorKind::ReadonlyRepository`.

---

## 11. Feature flags

```rust
pub struct FeatureFlag { /* private fields */ }
impl FeatureFlag {
    pub fn id(&self) -> u16;
    pub fn name(&self) -> &'static str;
    pub fn default_enabled(&self) -> bool;
    pub fn default_disabled(&self) -> bool;
    pub fn setting(&self) -> Option<bool>;
    pub fn in_default_state(&self) -> bool;
    pub fn enabled(&self) -> bool;          // setting.unwrap_or(default_enabled)
}
pub const MOVE_NODE_FLAG: u16 = 3;
pub const CREATE_TAG_FLAG: u16 = 4;
pub const DELETE_TAG_FLAG: u16 = 5;
pub fn find_feature_flag_id(flag: &str) -> IcechunkResult<u16>;
pub fn raise_if_feature_flag_disabled(repo_info: &RepoInfo, flag_id: u16,
                                      feature_description: &str) -> IcechunkResult<()>;

impl Repository {
    pub async fn feature_flags(&self)          -> RepositoryResult<impl Iterator<Item = FeatureFlag>>;
    pub async fn enabled_feature_flags(&self)  -> RepositoryResult<impl Iterator<Item = FeatureFlag>>;
    pub async fn disabled_feature_flags(&self) -> RepositoryResult<impl Iterator<Item = FeatureFlag>>;
    pub async fn set_feature_flag(&self, feature_flag: &str, set_to: Option<bool>)
        -> RepositoryResult<()>;
}
```

The registry is exactly three flags, **all default-enabled**:
`move_node` → 3, `create_tag` → 4, `delete_tag` → 5. `FeatureFlag` derives
`PartialEq` and `Debug`.

`set_feature_flag(name, Some(false))` disables, `Some(true)` force-enables,
`None` resets to the default. A name outside the registry is an error
(`IcechunkFormatErrorKind::InvalidFeatureFlagName`), as is an unknown id
(`InvalidFeatureFlagId`). Flags at their default are not serialised into the
table of contents, so `enabled_feature_flags()` on a fresh repository lists all
three (they are *effectively* enabled) while
`RepoInfo::enabled_feature_flags()` lists none (nothing was explicitly set).

A disabled flag makes the corresponding operation fail with
`IcechunkFormatErrorKind::FeatureFlagDisabled { feature_description, feature_flag }`,
surfaced as `RepositoryErrorKind::FormatError(..)` or
`SessionErrorKind::RepositoryError(RepositoryErrorKind::FormatError(..))`.
`feature_flag` is the flag's name. `feature_description` is a short phrase whose
value is **out of scope** except where this document names it:

| gate | `feature_flag` | `feature_description` |
| --- | --- | --- |
| `Repository::rearrange_session` | `move_node` | `"create rearrange session"` |
| committing a rearrange session | `move_node` | `"commit rearrange session"` |
| flushing a rearrange session | `move_node` | `"flush rearrange session"` |
| `Repository::create_tag` | `create_tag` | `"tag creation"` |
| `Repository::delete_tag` | `delete_tag` | `"tag delete"` |

Changes are persisted immediately and visible to any client that opens the
repository afterwards, and each one adds a `FeatureFlagChanged { id, new_value }`
entry to the operations log.

---

## 12. Repository-level metadata

```rust
impl Repository {
    pub async fn get_metadata(&self)    -> RepositoryResult<SnapshotProperties>;
    pub async fn set_metadata(&self, metadata: &SnapshotProperties) -> RepositoryResult<()>;
    pub async fn update_metadata(&self, metadata: &SnapshotProperties)
        -> RepositoryResult<SnapshotProperties>;
}
```

`SnapshotProperties` is `BTreeMap<String, serde_json::Value>`. `set_metadata`
**replaces** the whole map; `update_metadata` **merges** the given keys into it
and returns the merged map, which equals what `get_metadata` then reports. It
starts empty, is independent of a snapshot's own `properties`, survives reopening
the repository, and each write adds a `MetadataChangedUpdate` entry.

All three are second-generation-only (§16).

---

## 13. `vcc://` relative virtual chunk references

```rust
pub const VCC_RELATIVE_URL_SCHEME: &str = "vcc://";   // icechunk::format::manifest
impl VirtualChunkLocation {
    pub fn url(&self) -> &str;
    pub fn is_relative(&self) -> bool;
    pub fn parse_vcc(&self) -> Option<(&str, &str)>;                      // (name, relative path)
    pub fn from_vcc_path(container_name: &str, relative_path: &str)
        -> Result<VirtualChunkLocation, VirtualReferenceError>;
    pub fn from_url(path: &str) -> Result<VirtualChunkLocation, VirtualReferenceError>;
}
impl VirtualChunkContainer {
    pub fn new_named(name: String, url_prefix: String, store: ObjectStoreConfig)
        -> Result<Self, String>;
    pub fn new(url_prefix: String, store: ObjectStoreConfig) -> Result<Self, String>;
    pub fn name(&self) -> Option<&str>;
    pub fn url_prefix(&self) -> &str;
}
impl VirtualChunkResolver {
    pub fn new(containers: impl Iterator<Item = VirtualChunkContainer>,
               credentials: HashMap<String, Option<Credentials>>,
               settings: icechunk::storage::Settings) -> Self;
    pub fn expand_location(&self, location: &str) -> Result<String, VirtualReferenceError>;
}
impl RepositoryConfig {
    pub fn set_virtual_chunk_container(&mut self, cont: VirtualChunkContainer)
        -> Result<(), String>;
}
```

- `from_vcc_path("name", "a/b")` builds `vcc://name/a/b`. A name that is empty or
  contains `/` is rejected (`VirtualReferenceErrorKind::NoContainerForName`), and
  so is such a name in `VirtualChunkContainer::new_named` (an `Err(String)`).
- `is_relative()` is true only for a `vcc://` location; `parse_vcc()` splits it
  into `(name, rest)` and is `None` for an absolute URL. `from_url` accepts both
  forms.
- A `url_prefix` must end in `/` — `VirtualChunkContainer::{new, new_named}`
  reject one that does not.
- Resolution looks the container up **by name** and returns
  `format!("{url_prefix}{rest}")`; an unknown name is
  `VirtualReferenceErrorKind::NoContainerForName`. Reading a chunk whose
  container name is unknown fails. `VirtualChunkResolver::expand_location` is
  that resolution called directly: it returns an absolute URL unchanged and
  expands a `vcc://name/rest` location against the named container's prefix.
- `set_virtual_chunk_container` keys named containers **by name**: setting a
  container with an existing name replaces it, prefix and all. Re-pointing a
  named container at a different prefix therefore relocates every `vcc://`
  reference in the repository **without rewriting any manifest**, which is what
  the verifier asserts (write a reference against directory A, reopen with the
  same name bound to directory B, read the bytes from B).
- Authorisation still keys on **URL prefix**, not on name: the third argument of
  `Repository::{create, open, open_or_create}` is
  `HashMap<String, Option<Credentials>>` keyed by prefix.
- After a commit, `Session::all_virtual_chunk_locations()` reports **expanded,
  absolute** URLs.

`ManifestVirtualChunkLocationCompressionConfig` (`min_num_chunks` default 1000,
`dictionary_max_training_samples` 100, `dictionary_max_size_bytes` 2048,
`compression_level` 3), `Manifest::{uses_location_compression,
location_dictionary_size, num_compressed_refs}` and the zstd location dictionary
exist but are **out of scope** — see §20.

---

## 14. The first-to-second-generation migration

```rust
pub enum MigrationErrorKind {                      // #[non_exhaustive]
    RepositoryError(RepositoryErrorKind),
    RefError(RefErrorKind),
    FormatError(IcechunkFormatErrorKind),
    StorageError(StorageErrorKind),
    InvalidRepositoryMigration { expected: SpecVersionBin, target: SpecVersionBin,
                                 actual: SpecVersionBin },
    ReadonlyRepo,
    Unknown,
}
pub type MigrationError = ICError<MigrationErrorKind>;
pub type MigrationResult<A> = Result<A, MigrationError>;

pub async fn migrate_1_to_2(
    repo: Repository,                    // consumed; the handle is invalidated
    dry_run: bool,
    delete_unused_v1_files: bool,
    prefetch_concurrency: Option<usize>, // default 64
) -> MigrationResult<()>;
```

The verifier drives this against a **real, checked-in first-generation
repository** it supplies (a tree with `refs/`, `config.yaml`, `snapshots/`,
`manifests/`, `transactions/` and `chunks/`, laid out as
`/app/icechunk-spec/reference/spec-v1.md` describes and as milestone 03's own
`refs` writer wrote it — where those two differ, `PROFILE.md` R9-18 says which
wins), copied into a temporary directory first. It carries more than one branch,
more than one tag, and at least one tag that was deleted.

What is asserted:

- `dry_run = true` performs the whole computation and writes **no**
  second-generation file: the repository still reports `SpecVersionBin::V1`
  afterwards, `repo` does not exist and `config.yaml` still does.
- A real migration leaves `spec_version() == SpecVersionBin::V2`, and **every
  branch and tag still resolves to the same snapshot id**, with byte-identical
  ancestries (`SnapshotInfo` equality, which covers id, parent, timestamp,
  message and metadata).
- It reads and writes **no chunks**: the chunk references reachable from a branch
  are identical before and after, the set of objects under `chunks/` is
  unchanged, and every materialised reference still reads back.
- A deleted tag's name is carried across into `RepoInfo::deleted_tags()`.
- The operations log is reconstructed: newest entry is
  `RepoMigratedUpdate { from_version: V1, to_version: V2 }`, the log contains a
  `RepoInitializedUpdate`, one `NewCommitUpdate` per non-root snapshot, a
  `BranchCreatedUpdate` per non-default branch, and a `TagCreatedUpdate` /
  `TagDeletedUpdate` pair for a tag that was created and later deleted, with the
  deletion newer than the creation. Timestamps are strictly decreasing.
- Migrating an already-second-generation repository is
  `MigrationErrorKind::InvalidRepositoryMigration { actual: V2, .. }`, and leaves
  the repository usable.

`delete_unused_v1_files = true` additionally removes `config.yaml` and the
`refs/` prefix on a best-effort basis; with `false` the `refs/` prefix survives
and `refs::list_branches` can still read it. The deletion **is** asserted, and
only through that layout: `config.yaml` and the `refs/` prefix are gone after a
migration with `true` and present after one with `false`. Its best-effort nature
— what the migration does when one of those deletes fails — is not asserted,
because the verifier has no way to make a delete fail.

---

## 15. Ancestry graph rendering

```rust
impl Repository {
    pub async fn ancestry_graph(&self, version: Option<&VersionInfo>, plain: bool)
        -> RepositoryResult<AncestryGraph>;
}

pub struct AncestryNode {
    pub info: SnapshotInfo,
    pub branches: Vec<String>,
    pub tags: Vec<String>,
    pub column: usize,
}
pub struct AncestryGraph {
    pub nodes: Vec<AncestryNode>,     // display order, newest first
    pub num_columns: usize,
    pub all_branches: Vec<String>,    // sorted, "main" first
    pub total_snapshots: usize,
    pub plain: bool,
}
impl AncestryGraph {
    pub fn new(branch_ancestries: Vec<(String, Vec<SnapshotInfo>)>,
               tag_map: &HashMap<SnapshotId, Vec<String>>,
               all_branches: Vec<String>, plain: bool) -> Self;
    pub fn column_colors(&self) -> Vec<usize>;
    pub fn layout(&self) -> Vec<LayoutElement>;
    pub fn to_svg(&self) -> String;
}
impl std::fmt::Display for AncestryGraph { … }
pub enum LayoutElement {
    Node { row: usize, col: usize, node_idx: usize },
    Line { from_row: usize, to_row: usize, col: usize },
    Fork { from_row: usize, from_col: usize, to_row: usize, to_col: usize },
}
```

- `Some(BranchTipRef(b))` renders that branch's linear ancestry in **one**
  column; `Some(TagRef|SnapshotId)` renders an anonymous single column; `None`
  renders every branch as a tree with one column per branch and `main` as column
  0, with `all_branches` sorted `main`-first then alphabetically.
- `plain = true` renders with **no ANSI escape sequences**; `plain = false`
  renders with them. The verifier tests for the presence or absence of `\u{1b}`,
  and that the rendering contains the commit messages, branch names and tag names
  of the nodes it shows. It asserts nothing about glyphs, spacing or colour
  values.
- `to_svg()` returns a string containing `<svg` … `</svg>` and the commit
  messages.
- Anonymous (detached) snapshots do **not** appear in the graph.
- `column_colors()` has one entry per column; `layout()` is non-empty for a
  non-empty graph. An empty graph renders `"(empty history)"` — **not asserted**:
  every repository has at least the initial snapshot, so the only way to build an
  empty graph is `AncestryGraph::new` called directly, which §20 excludes.

---

## 16. On-disk generations, and what they gate

```rust
pub enum SpecVersionBin { V1 = 1u8, #[default] V2 = 2u8 }   // Display: "1.0" / "2.0"
impl SpecVersionBin { pub fn current() -> Self; }           // == V2
impl TryFrom<u8> for SpecVersionBin { type Error = IcechunkFormatErrorKind; }

pub enum DetectedSpecVersion {
    V1,
    V2Plus { version: SpecVersionBin, repo_info: Arc<RepoInfo> },
}
impl DetectedSpecVersion { pub fn spec_version(&self) -> SpecVersionBin; }

impl Repository {
    pub async fn create(config: Option<RepositoryConfig>,
                        storage: Arc<dyn Storage + Send + Sync>,
                        authorize_virtual_chunk_access: HashMap<String, Option<Credentials>>,
                        spec_version: Option<SpecVersionBin>,
                        check_clean_root: bool) -> RepositoryResult<Self>;
    pub async fn open(config: Option<RepositoryConfig>,
                      storage: Arc<dyn Storage + Send + Sync>,
                      authorize_virtual_chunk_access: HashMap<String, Option<Credentials>>)
                      -> RepositoryResult<Self>;
    pub async fn open_or_create(config, storage, auth,
                                create_version: Option<SpecVersionBin>,
                                check_clean_root: bool) -> RepositoryResult<Self>;
    pub async fn exists(storage: Arc<dyn Storage + Send + Sync>,
                        settings: Option<icechunk::storage::Settings>) -> RepositoryResult<bool>;
    pub async fn fetch_spec_version(storage: Arc<dyn Storage + Send + Sync>,
                                    settings: Option<icechunk::storage::Settings>)
        -> RepositoryResult<Option<DetectedSpecVersion>>;
    pub fn spec_version(&self) -> SpecVersionBin;
    pub async fn reopen(&self, config: Option<RepositoryConfig>,
                        auth: Option<HashMap<String, Option<Credentials>>>)
                        -> RepositoryResult<Self>;            // now async
    pub fn from_bytes(bytes: &[u8]) -> RepositoryResult<Self>;
    pub fn as_bytes(&self) -> RepositoryResult<Vec<u8>>;
    pub async fn fetch_config(storage: Arc<dyn Storage + Send + Sync>)
        -> RepositoryResult<Option<(RepositoryConfig, icechunk::storage::VersionInfo)>>;
    pub async fn save_config(&self) -> RepositoryResult<icechunk::storage::VersionInfo>;
    pub async fn lookup_manifest_files(&self, snapshot_id: &SnapshotId)
        -> RepositoryResult<impl Iterator<Item = ManifestFileInfo>>;
}
```

- `create(.., spec_version = None, ..)` writes a **second-generation**
  repository. `Some(SpecVersionBin::V1)` still writes a first-generation one, and
  it must remain fully readable *and* writable: commits, branches, tags,
  expiration and garbage collection all dispatch on the generation.
- `fetch_spec_version` returns `None` for an empty prefix, `Some(V1)` for a
  first-generation repository and `Some(V2Plus { .. })` for a second-generation
  one. `exists` agrees with it.
- The second-generation-only operations are `get_metadata`, `set_metadata`,
  `update_metadata`, `get_status`, `set_status`, `feature_flags`,
  `enabled_feature_flags`, `disabled_feature_flags`, `set_feature_flag`,
  `ops_log`, `rearrange_session`, `move_node`, `AssetManager::fetch_repo_info`,
  and `CommitBuilder::amend`. On a first-generation repository each fails with
  `RepositoryErrorKind::BadRepoVersion { minimum_spec_version: SpecVersionBin::V2 }`
  (wrapped in `SessionErrorKind::RepositoryError` where it surfaces through a
  session).
- There is **no forward-compatibility refusal any more.** A repository with a
  `repo` object is what this release writes; the milestone-08 error
  `RepositoryErrorKind::RepositoryIsV2` is gone.

---

## 17. Error machinery

```rust
pub struct ICError<E> { pub kind: E, pub context: SpanTrace }
impl<E> ICError<E> {
    pub fn capture(kind: E) -> Self;                 // replaces the removed `new`
    pub fn kind(&self) -> &E;
    pub fn span(&self) -> &SpanTrace;
    pub fn inject<F>(self) -> ICError<F> where E: Into<F>;
}
```

**The blanket `impl<E: Into<Kind>> From<E> for ICError<Kind>` impls are
deliberately deleted.** Consequences you will hit while writing code:

- `SomeErrorKind::X.into()` no longer produces a `SomeError`; use
  `SomeError::capture(SomeErrorKind::X)`.
- `?` no longer converts between error kinds; use `ICError::inject`, or the
  `ICResultExt::capture` / `ICResultCtxExt::inject` combinators on `Result`
  (those two traits live in `icechunk_types` and are **not** re-exported through
  `icechunk`, so a caller outside the workspace uses `ICError::{capture, inject}`
  directly).

`RepositoryErrorKind`, `SessionErrorKind`, `MigrationErrorKind` and
`VersionInfo` are `#[non_exhaustive]`, so an exhaustive `match` on any of them
does not compile from outside the crate.

Error kinds the verifier matches on (category only — never the message text,
except the four `InvalidCommitConfiguration` reasons in §6 and the `"first
commit"` substring in §6.1):

- `RepositoryErrorKind::{Conflict, CannotDeleteMain, ReadonlyRepository,
  RepositoryDoesntExist, BadRepoVersion, NoAmendForInitialCommit,
  Ref(RefErrorKind::RefNotFound), FormatError}`
- `SessionErrorKind::{Conflict, RebaseFailed, NoChangesToCommit,
  NoAmendForInitialCommit, CommitNotAllowed, MergeNotAllowed,
  CannotForkReadOnlySession, RearrangeSessionOnly, NonRearrangeSession,
  MoveWontOverwrite, InvalidIndex, InvalidCommitConfiguration, ReadOnlySession,
  RepositoryError}`
- `IcechunkFormatErrorKind::{FeatureFlagDisabled, InvalidFeatureFlagName,
  InvalidFeatureFlagId, InvalidSpecVersion}`
- `StoreErrorKind::BadChunkGridMetadata`
- `MigrationErrorKind::InvalidRepositoryMigration`
- `GCError::Repository(..)`, wrapping `RepositoryErrorKind::ReadonlyRepository`
  (§10, §18.2)

Kinds this document names elsewhere that **no group matches on**:
`RepositoryErrorKind::RepoUpdateAttemptsLimit` (the retry schedule is out of
scope — §20) and `IcechunkFormatErrorKind::{BranchNotFound, TagNotFound,
TagPreviouslyDeleted, SnapshotIdNotFound}`. Where one of those is observable at
all the verifier checks only that the call failed — re-creating a deleted tag
(§3.1), an unknown virtual-chunk-container name (§13) — so the shape you give
them costs nothing.

`Repository::lookup_branch` on a missing branch is
`RepositoryErrorKind::Ref(RefErrorKind::RefNotFound(name))` on **both**
generations — the second generation converts the table of contents'
`BranchNotFound` into that shape deliberately.

---

## 18. The inherited contract

Every milestone this step inherits is re-exercised here, through the v2.0.0
surface. The obligations, restated as this release spells them:

### 18.1 Milestones 01–02 — the repository, sessions, conflicts, virtual chunks

`Repository::{create, open, exists}`; `create_branch`, `list_branches`,
`lookup_branch`, `reset_branch`, `delete_branch` (deleting `main` is
`CannotDeleteMain`), `create_tag`, `list_tags`, `lookup_tag`, `delete_tag`;
creating an existing branch or tag fails. `list_branches()` and `list_tags()`
both return `RepositoryResult<BTreeSet<String>>` — so they are **ordered by
name**, they hold no duplicates, and `list_tags` hides deleted tags — and they
agree element for element with `RepoInfo::{branch_names, tag_names}` (§3.1). `writable_session` / `readonly_session`
/ `Store::from_session` with `Store::{get, set, exists, list, list_prefix,
list_dir, delete, set_virtual_ref}`; a read-only store rejects `set`.
Time travel by `VersionInfo::{SnapshotId, TagRef, BranchTipRef, AsOf}` — all four
resolve through `Repository::resolve_version`, and `AsOf { branch, at }` selects
the newest snapshot at or before `at`. Conflicts: a second commit from the same
base is `SessionErrorKind::Conflict`; `Session::rebase(&ConflictDetector)`
succeeds on disjoint changes and is `SessionErrorKind::RebaseFailed` on the same
chunk; `BasicConflictSolver { on_chunk_conflict: VersionSelection::UseOurs, .. }`
resolves it, both standalone and through `CommitBuilder::rebase`. Virtual chunk
containers: an unnamed container over a `file://` prefix, a location built with
`VirtualChunkLocation::from_url`, `Store::set_virtual_ref(key, ref, validate)`
where `validate = true` rejects a location no container covers, and
`Repository::authorized_virtual_container_prefixes`.

### 18.2 Milestone 03 — expiration and garbage collection

```rust
pub async fn expire(asset_manager: Arc<AssetManager>, older_than: DateTime<Utc>,
                    expired_branches: ExpiredRefAction, expired_tags: ExpiredRefAction,
                    repo_update_retries: Option<&RepoUpdateRetryConfig>,
                    num_updates_per_repo_info_file: u16) -> GCResult<ExpireResult>;
pub async fn garbage_collect(asset_manager: Arc<AssetManager>, config: &GCConfig,
                             repo_update_retries: Option<&RepoUpdateRetryConfig>,
                             num_updates_per_repo_info_file: u16) -> GCResult<GCSummary>;
```

```rust
#[derive(Debug, thiserror::Error)]
pub enum GCError {
    Ref(#[from] RefError),
    Repository(#[from] RepositoryError),
    FormatError(#[from] IcechunkFormatError),
    StorageError(#[from] StorageError),
}
pub type GCResult<A> = Result<A, GCError>;
```

`GCConfig::{new, clean_all}` are unchanged. Expiration releases snapshots older
than the cutoff without moving a branch tip and without losing the tip's data;
garbage collection deletes what no reference reaches and reports it in
`GCSummary`; `dry_run = true` reports the same counts and deletes nothing.
Both refuse to run on a second-generation repository whose status is not
`Online` (§10); the refusal arrives as
`GCError::Repository(_)` carrying `RepositoryErrorKind::ReadonlyRepository`.

### 18.3 Milestone 04 — the FlatBuffers format and its file headers

A commit writes snapshot, manifest, transaction-log and chunk objects, and a
fresh `Repository::open` reads the same bytes back.
`Repository::lookup_manifest_files(&snapshot)` names the snapshot's manifests
(`SnapshotInfo::manifests` is **gone** — §19.7). `Repository::diff` reports the
transaction log's content. Every metadata object begins with a 39-byte header:
12 magic bytes, 24 bytes of client name, then **byte 36 = the format
generation**, byte 37 = the file type, byte 38 = the compression algorithm.
A generation byte the library does not support is
`IcechunkFormatErrorKind::InvalidSpecVersion { found, max_supported }` —
patching byte 36 of a snapshot object must make reading that snapshot fail
rather than mis-parse.

### 18.4 Milestone 05 — the command-line interface

The binary is built with `--features cli` and the verifier finds it through the
environment variable **`ICECHUNK_TEST_CLI_BIN`**, which holds its absolute path.
Its configuration file is `$XDG_CONFIG_HOME/icechunk/cli-config.yaml` (the
verifier sets `XDG_CONFIG_HOME` and `HOME` to a temporary directory, so the real
one is never touched).

Grammar, all of which must keep working:

```
icechunk --help
icechunk repo create <alias>
icechunk snapshot list <alias> [-n <count>] [-b <branch>]
icechunk config init [-f|--force]
icechunk config add <alias>
icechunk config list
```

Exit status `0` on success, non-zero on failure — an unknown subcommand, an
alias the configuration does not name, or a repository that cannot be opened.
`--help` names the three command groups `repo`, `snapshot` and `config`.
`config list` prints the configuration as YAML, including the alias and the
configured path. `repo create <alias>` creates the repository the alias names, so
the library can then open it. `snapshot list <alias>` prints one line per
snapshot, newest first, each containing that snapshot's commit message; `-n`
bounds how many lines are printed.

The configuration file is a YAML map `repos: { <alias>: <definition> }`, where a
definition is an externally tagged enum serialised as a **YAML tag**, `!Variant`
— not as a nested mapping:

```yaml
repos:
  demo: !LocalFileSystem
    path: /path/to/repo
    config: {}
```

The other variants are `S3`, `Tigris`, `GCS` and `Azure`; only
`LocalFileSystem` is exercised. `config: {}` must deserialise to a default
`RepositoryConfig`. A configuration file that cannot be read or parsed is
treated as empty, so every alias then fails to resolve — `config list` still
succeeds and lists nothing, and every other subcommand fails on the alias.

`config init` and `config add` **prompt on the terminal** for the repository's
type and its fields, so the verifier runs neither: what it asserts of those two
is that `icechunk config --help` names them alongside `list`. Everything else in
the grammar is driven as a process with a pipe for stdin and stdout.

### 18.5 Milestone 06 — manifest splitting

`ManifestConfig { preload, splitting, virtual_chunk_location_compression }` with
`ManifestSplittingConfig { split_sizes: Option<Vec<(ManifestSplitCondition,
Vec<ManifestSplitDim>)>> }`, `ManifestSplitCondition::{Or, And, PathMatches,
NameMatches, AnyArray}` and `ManifestSplitDim { condition:
ManifestSplitDimCondition::{Axis, DimensionName, Any}, num_chunks: u32 }`. A
configuration round-trips through `save_config` / `Repository::open`. An array of
four chunks split every two chunks produces **two** manifest files
(`lookup_manifest_files(&snapshot).count() == 2`), and every chunk still reads
back; an array whose chunks all fall inside a single split — or one committed
under the default configuration, as in §18.3 — produces exactly **one**.
`ops::manifests::rewrite_manifests` re-splits an existing snapshot under a new
configuration without changing any chunk. It gained the two parameters §6's
commit builder introduced, and `max_concurrent_manifests` is a plain `usize`
here — **not** a `NonZeroU16` like the `max_concurrent_*` settings knobs of
§18.6 and §18.7, and not an `Option`:

```rust
// icechunk::ops::manifests
pub async fn rewrite_manifests(repository: &Repository, branch: &str, message: &str,
                               max_concurrent_manifests: usize,
                               properties: Option<SnapshotProperties>,
                               commit_method: CommitMethod) -> ManifestOpsResult<SnapshotId>;
// `ManifestOpsResult<A> = Result<A, ManifestOpsError>` is milestone 06's alias;
// `CommitMethod` is §6's.
```

### 18.6 Milestones 07–08 — the asynchronous API, backends and settings

`new_in_memory_storage()` and `new_local_filesystem_storage(&std::path::Path)`
are `async` and return `StorageResult<Arc<dyn Storage + Send + Sync>>`. On the
`Storage` trait, `can_write()` and `default_settings()` are **async and
fallible**, `root_is_clean(&Settings)` takes the settings and reports `true` for
a prefix holding no repository and `false` once one has been created there, and
`storage_info() -> StorageInfo` is required. `Settings { concurrency, retries,
timeouts, … }` with `RetriesSettings { max_tries, initial_backoff_ms,
max_backoff_ms }`, `TimeoutSettings { connect_timeout_ms, read_timeout_ms,
operation_timeout_ms, operation_attempt_timeout_ms }` and
`ConcurrencySettings { max_concurrent_requests_for_object,
ideal_concurrent_request_size }`, all reachable through
`Repository::storage_settings()` after being set in
`RepositoryConfig::storage`. `RepositoryConfig::max_concurrent_requests()`
defaults to `DEFAULT_MAX_CONCURRENT_REQUESTS = 256`. `ObjectStoreConfig` keeps
its eight variants — `InMemory`, `LocalFileSystem`, `Http`, `S3Compatible`, `S3`,
`Gcs`, `Azure`, `Tigris` — and round-trips through serde. Opening an empty prefix
is `RepositoryErrorKind::RepositoryDoesntExist`.

### 18.7 Inherited and unchanged — the accessors the verifier still calls

These carried forward from earlier milestones without a signature change at this
release, so §19 has nothing to say about them. They are listed because the
verifier reaches for them and nothing else in this document names them.

```rust
impl Repository {
    pub fn config(&self) -> &RepositoryConfig;
    pub async fn lookup_snapshot(&self, id: &SnapshotId) -> RepositoryResult<SnapshotInfo>;
    // The handle the repository was built on, so a caller can reach `refs::*`
    // and `Storage`'s own methods without rebuilding it.
    pub fn storage(&self) -> &Arc<dyn Storage + Send + Sync>;
}
pub struct SnapshotInfo { /* … */ pub flushed_at: DateTime<Utc>, /* … */ }

impl Session {
    pub async fn get_node(&self, path: &Path)  -> SessionResult<NodeSnapshot>;
    pub async fn get_array(&self, path: &Path) -> SessionResult<NodeSnapshot>;
    pub async fn get_group(&self, path: &Path) -> SessionResult<NodeSnapshot>;
    // Recursive, and it yields `parent_group` itself as well as every node
    // beneath it, so `list_nodes(&Path::root())` counts the whole hierarchy
    // including the root. The verifier asserts exact counts.
    pub async fn list_nodes(&self, parent_group: &Path)
        -> SessionResult<impl Iterator<Item = SessionResult<NodeSnapshot>>>;
    pub async fn all_chunks(&self)
        -> SessionResult<impl Stream<Item = SessionResult<(Path, ChunkInfo)>>>;
}

// Inherited struct shapes the verifier builds or reads field by field. All of
// these fields have been `pub` since the milestone that introduced them and
// still are; §19 changes none of them.
pub struct NodeSnapshot { pub id: NodeId, pub path: Path, pub user_data: Bytes,
                          pub node_data: NodeData }
pub enum NodeData {
    Array { shape: ArrayShape, dimension_names: Option<Vec<DimensionName>>,
            manifests: Vec<ManifestRef> },
    Group,
}
pub struct ChunkIndices(pub Vec<u32>);                         // icechunk::format
pub struct ChunkInfo { pub node: NodeId, pub coord: ChunkIndices,
                       pub payload: ChunkPayload }
pub struct VirtualChunkRef { pub location: VirtualChunkLocation,
                             pub offset: ChunkOffset, pub length: ChunkLength,
                             pub checksum: Option<Checksum> }
// `PROFILE.md` R7-4: the Rust fields have been `pub` throughout.
pub struct ManifestFileInfo { pub id: ManifestId, pub size_bytes: u64,
                              pub num_chunk_refs: u32 }
pub struct RefData { pub snapshot: SnapshotId }                // icechunk::refs
pub struct ETag(pub String);            // icechunk::storage, from `icechunk_types`
pub struct Generation(pub String);      // icechunk::storage
pub struct S3Options { pub region: Option<String>, pub endpoint_url: Option<String>,
                       pub anonymous: bool, pub allow_http: bool,
                       pub force_path_style: bool,
                       pub network_stream_timeout_seconds: Option<u32>,
                       pub requester_pays: bool }              // icechunk::config

// `RepositoryConfig`, `ManifestConfig`, `ManifestSplittingConfig`,
// `storage::{Settings, RetriesSettings, TimeoutSettings, ConcurrencySettings}`
// are all plain structs of public `Option` fields, so a caller outside the crate
// builds one with a struct literal and `..Default::default()` and reads the
// effective value back through the accessor of the same name.
pub struct RepositoryConfig { pub inline_chunk_threshold_bytes: Option<u16>,
    pub get_partial_values_concurrency: Option<u16>,
    pub compression: Option<CompressionConfig>,
    pub max_concurrent_requests: Option<u16>, pub caching: Option<CachingConfig>,
    pub storage: Option<storage::Settings>,
    pub virtual_chunk_containers: Option<HashMap<String, VirtualChunkContainer>>,
    pub manifest: Option<ManifestConfig>, pub previous_file: Option<String>,
    pub repo_update_retries: Option<RepoUpdateRetryConfig>,
    pub num_updates_per_repo_info_file: Option<u16> }
impl RepositoryConfig {
    pub fn manifest(&self) -> &ManifestConfig;                 // default if unset
    pub fn storage(&self)  -> Option<&storage::Settings>;      // NOT defaulted
    pub fn max_concurrent_requests(&self) -> u16;              // default 256
}
pub struct ManifestConfig { pub preload: Option<ManifestPreloadConfig>,
    pub splitting: Option<ManifestSplittingConfig>,
    pub virtual_chunk_location_compression:
        Option<ManifestVirtualChunkLocationCompressionConfig> }
impl ManifestConfig { pub fn splitting(&self) -> &ManifestSplittingConfig; }
pub struct ManifestSplittingConfig {
    pub split_sizes: Option<Vec<(ManifestSplitCondition, Vec<ManifestSplitDim>)>> }
pub struct ManifestSplitDim { pub condition: ManifestSplitDimCondition,
                              pub num_chunks: u32 }
pub struct Settings { pub concurrency: Option<ConcurrencySettings>,
    pub retries: Option<RetriesSettings>, pub timeouts: Option<TimeoutSettings>,
    pub unsafe_use_conditional_update: Option<bool>,
    pub unsafe_use_conditional_create: Option<bool>,
    pub unsafe_use_metadata: Option<bool>, pub storage_class: Option<String>,
    pub metadata_storage_class: Option<String>,
    pub chunks_storage_class: Option<String>,
    pub minimum_size_for_multipart_upload: Option<u64> }
pub struct RetriesSettings { pub max_tries: Option<NonZeroU16>,
    pub initial_backoff_ms: Option<u32>, pub max_backoff_ms: Option<u32> }
pub struct TimeoutSettings { pub connect_timeout_ms: Option<u32>,
    pub read_timeout_ms: Option<u32>, pub operation_timeout_ms: Option<u32>,
    pub operation_attempt_timeout_ms: Option<u32> }

// §18.2's "unchanged" constructor, spelled out because the verifier calls it.
impl GCConfig {
    pub fn clean_all(chunks_age: DateTime<Utc>, metadata_age: DateTime<Utc>,
                     extra_roots: Option<HashSet<SnapshotId>>,
                     max_snapshots_in_memory: NonZeroU16,
                     max_compressed_manifest_mem_bytes: NonZeroUsize,
                     max_concurrent_manifest_fetches: NonZeroU16,
                     dry_run: bool) -> Self;
}
pub enum ExpiredRefAction { Delete, Ignore }   // `Ignore` leaves the ref in place
// `prefix` is a key prefix in the store's own key space, which carries NO leading
// slash: `list_prefix("array")` yields `array/zarr.json`, `array/c/0`, … and
// `list_dir("array")` yields the immediate names `zarr.json` and `c`.
impl Store {
    pub async fn read_only(&self) -> bool;
    pub async fn delete(&self, key: &str) -> StoreResult<()>;
    pub async fn list_prefix(&self, prefix: &str)
        -> StoreResult<impl Stream<Item = StoreResult<String>>>;
    pub async fn list_dir(&self, prefix: &str)
        -> StoreResult<impl Stream<Item = StoreResult<String>>>;
}
impl ByteRange { pub fn bounded(start: ChunkOffset, end: ChunkOffset) -> Self; }  // and `ALL`
// `RepositoryConfig::inline_chunk_threshold_bytes` is a `pub Option<u16>` field
// with an accessor of the same name applying the default of 512. Setting it to
// `Some(0)` forces every chunk to be written as its own object.
impl RepositoryConfig { pub fn inline_chunk_threshold_bytes(&self) -> u16; }

pub struct ExpireResult { pub released_snapshots: HashSet<SnapshotId>,
                          pub edited_snapshots: HashSet<SnapshotId>,
                          pub deleted_refs: HashSet<Ref> }
pub struct GCSummary { pub bytes_deleted: u64, pub chunks_deleted: u64,
                       pub manifests_deleted: u64, pub snapshots_deleted: u64,
                       pub attributes_deleted: u64, pub transaction_logs_deleted: u64 }
                                                   // derives Debug, PartialEq, Eq, Default
pub struct StorageInfo { pub backend_type: &'static str,
                         pub fields: Vec<(&'static str, String)> }

// `PROFILE.md` R5-2's "native, virtual and inline bytes in separate fields".
pub struct ChunkStorageStats { pub native_bytes: u64, pub virtual_bytes: u64,
                               pub inlined_bytes: u64 }   // Debug, Default, PartialEq
pub async fn icechunk::ops::stats::repo_chunks_storage(
    asset_manager: Arc<AssetManager>, max_snapshots_in_memory: NonZeroU16,
    max_compressed_manifest_mem_bytes: NonZeroUsize,
    max_concurrent_manifest_fetches: NonZeroU16) -> RepositoryResult<ChunkStorageStats>;

// `PROFILE.md` R8-11's public consequence. `CachingConfig` derives `Default`.
impl CachingConfig { pub fn num_snapshot_nodes(&self) -> u64; }   // default 500_000

// `storage::Settings`' fields are `Option`s; these accessors apply the defaults.
impl icechunk::storage::Settings {
    pub fn concurrency(&self) -> &ConcurrencySettings;
    pub fn retries(&self)     -> &RetriesSettings;
    pub fn timeouts(&self)    -> Option<&TimeoutSettings>;
}
impl RetriesSettings {
    pub fn max_tries(&self) -> NonZeroU16;
    pub fn initial_backoff_ms(&self) -> u32;
    pub fn max_backoff_ms(&self) -> u32;
}
impl ConcurrencySettings {
    pub fn max_concurrent_requests_for_object(&self) -> NonZeroU16;
    pub fn ideal_concurrent_request_size(&self) -> NonZeroU64;
}

// Every `refs::*` free function takes the storage and its settings and addresses
// FIRST-generation on-disk refs only (§19.8).
pub async fn icechunk::refs::list_branches(storage: &(dyn Storage + Send + Sync),
                                           storage_settings: &storage::Settings)
    -> RefResult<BTreeSet<String>>;   // and `list_tags`, `list_refs`, `fetch_branch_tip_v1`
```

Three behaviours of that surface the verifier also relies on, none of them new
here:

- **`Store::get` returns what `Store::set` wrote, byte for byte.** That holds for
  a Zarr metadata document as much as for a chunk: writing a `zarr.json` and
  reading the same key back yields the identical bytes.
- **The `zarr.json` document `Store::set` accepts** is an ordinary Zarr v3 array
  or group document. Outside `chunk_grid`, which §9 covers in full, an array
  document carries `zarr_format`, `node_type`, `shape`, `data_type`,
  `chunk_key_encoding`, `fill_value` and `codecs`, and the store stores the
  document it was given rather than a re-serialisation of it.
- **`Session::all_node_chunks(&NodeSnapshot)` and `Snapshot::get_node_index(&Path)`
  are unchanged and still public.** Milestone 08 added both; §19 does not touch
  them, so the preservation rule keeps them. No group at this milestone calls
  either — they are **out of scope here**, which is a scope decision and not a
  removal: deleting them would still break the workspace this step hands on.

---

## 19. Migrating from milestone 08

This is the largest set of breaking changes in the ladder. Everything here is a
change to a previously public item.

### 19.1 Two changes that compile but mean something different

These are the dangerous ones: the old call site still type-checks.

1. **`DimensionShape::new(len, chunk_length: NonZeroU64)` became
   `DimensionShape::new(len, num_chunks: u32)`**, and
   `ArrayShape::new<I: IntoIterator<Item = (u64, u64)>>` became
   `IntoIterator<Item = (u64, u32)>`. `ArrayShape::new(vec![(20, 10)])` compiles
   under both and meant *"length 20, chunks of 10 elements"* (two chunks) at
   milestone 08; here it means *"length 20, ten chunks"*.
   `DimensionShape::chunk_length()` is gone; use `num_chunks()`.
   The **validity boundary for chunk coordinates moves with it**: for that same
   call, chunk index 9 was out of range at milestone 08 and is the last valid
   index here, and writing chunk index 10 is now
   `SessionErrorKind::InvalidIndex { coords, path }` (§9).
2. **The blanket `From<E> for ICError<Kind>` impls are gone** (§17), so `?` and
   `.into()` between error kinds no longer compile. This one does *not* compile
   silently — but the fix is not the obvious one: it is `ICError::capture` /
   `ICError::inject`, not a new `From` impl.

### 19.2 Format generation

`SpecVersionBin` had one variant, `V0dot1 = 1u8`; it now has
`{ V1 = 1u8, #[default] V2 = 2u8 }`, gained `Display`, `PartialOrd`/`Ord`, and
its `TryFrom<u8>::Error` changed from `String` to `IcechunkFormatErrorKind`.
`current()` returns `V2`. The default written generation flipped.

### 19.3 `Repository`

| milestone 08 | milestone 09 |
| --- | --- |
| `create(config, storage, auth)` | **+2 params**: `spec_version: Option<SpecVersionBin>, check_clean_root: bool` |
| `open_or_create(config, storage, auth)` | **+2 params**: `create_version, check_clean_root` |
| `exists(&dyn Storage)` | `exists(Arc<dyn Storage + Send + Sync>, Option<Settings>)` |
| `reopen(&self, ..)` — sync | **async** |
| `from_bytes(Vec<u8>)` | `from_bytes(&[u8])` |
| `fetch_config(&dyn Storage)` | `fetch_config(Arc<dyn Storage + Send + Sync>)` |
| `ancestry<'a>(&'a self, ..) -> impl Stream + 'a` | `ancestry(&self, ..) -> impl Stream + Send + use<>` (no borrow) |
| `ancestry_arc`, `snapshot_ancestry`, `snapshot_ancestry_arc`, `resolve_ref_version` | **removed** |
| `raise_if_invalid_snapshot_id` (free fn) | **removed** |
| — | added: `fetch_spec_version`, `spec_version`, `lookup_manifest_files`, `ancestry_graph`, `ops_log`, `get_metadata`, `set_metadata`, `update_metadata`, `get_status`, `set_status`, `feature_flags`, `enabled_feature_flags`, `disabled_feature_flags`, `set_feature_flag`, `rearrange_session` |

### 19.4 `Session`

| milestone 08 | milestone 09 |
| --- | --- |
| `async fn commit(&mut self, &str, Option<SnapshotProperties>) -> SessionResult<SnapshotId>` | `fn commit(&mut self, impl Into<String>) -> CommitBuilder<'_>`, finished with `.execute().await` |
| `async fn flush(msg, props)` | `commit(msg).anonymous().execute()` |
| `async fn rewrite_manifests(msg, props)` | `commit(msg).rewrite_manifests().execute()` |
| `async fn commit_rebasing(solver, attempts, msg, props, before, after)` | `commit(msg).rebase(solver, attempts).before_rebase_hook(h).after_rebase_hook(h).execute()`; hooks are the boxed `RebaseHook`, not generic closures |
| `fn changes(&self) -> &ChangeSet` | **removed**; use `has_uncommitted_changes()` / `status()` |
| `fn discard_changes(&mut self) -> ChangeSet` | `-> SessionResult<()>` |
| `fn lookup_splits(&self, &NodeId)` | **removed** |
| `fn from_bytes(Vec<u8>)` | `from_bytes(&[u8])` |
| `get_chunk_reader(..) -> SessionResult<Option<Pin<Box<..>>>>` | `-> SessionResult<Option<compat::IcechunkBoxFuture<'_, SessionResult<Bytes>>>>` — now borrows `self` |
| `get_chunk_writer(&self) -> impl FnOnce(..)` | `-> SessionResult<impl FnOnce(Bytes) -> IcechunkBoxFuture<'static, SessionResult<ChunkPayload>> + use<>>` — now returns a `Result` |
| `rebase` on a branch-less session → `ReadOnlySession` | → `CommitNotAllowed` |
| — | added: `mode`, `fork`, `is_fork`, `move_node`, `reindex_array`, `shift_array`, `create_rearrange_session` |

`create_writable_session`'s `branch_name` parameter is now `Option<String>`.

### 19.5 Errors

`SessionErrorKind::Ref(..)` was renamed `SessionErrorKind::RefError(..)`;
`SessionErrorKind::IncompatibleSplittingConfig` and
`RepositoryErrorKind::RepositoryIsV2` were removed. New `RepositoryErrorKind`
variants: `RepoInfoUpdated`, `ReadonlyRepository(String)`,
`NoAmendForInitialCommit`, `RepoUpdateAttemptsLimit(u64)`, `BadRepoVersion`,
`PoisonLock`. New `SessionErrorKind` variants: `CommitNotAllowed`,
`MergeNotAllowed`, `CannotForkReadOnlySession`, `RearrangeSessionOnly`,
`NonRearrangeSession`, `MoveWontOverwrite`, `NoAmendForInitialCommit`,
`ManifestsInconsistencyError`, `SessionMerge`, `InvalidByteRange`,
`InvalidCommitConfiguration`, `Other`.

### 19.6 `ChangeSet`

`icechunk::change_set::ChangeSet` changed **from a struct to an enum**:
`{ Edit(EditChanges), Rearrange(MoveTracker) }`. `ChangeSet::default()` no longer
yields a usable edit set — use `ChangeSet::for_edits()` /
`ChangeSet::for_rearranging()`. Its mutators became fallible
(`SessionResult<()>`); `merge_many`, `undo_update`,
`new_array_chunk_iterator` and `modified_manifest_extents_iterator` were removed;
`new_arrays()` now yields `(&Path, &NodeId, &ArrayData)`. The free function
`transaction_log_from_change_set(&SnapshotId, &ChangeSet) -> TransactionLog`
replaces `TransactionLog::new`.

### 19.7 `format`

`Path`/`PathError` moved to `icechunk_types` (same path through
`icechunk::format`). `ByteRange::slice(&self, bytes: &Bytes) -> Bytes` now takes
a reference. `NodeType` moved to the format crate root.
`SnapshotInfo::manifests` was **removed** — use
`Repository::lookup_manifest_files`. `Snapshot::initial` and
`Snapshot::from_buffer` gained a spec-version parameter; `Snapshot::get_node`
returns `IcechunkResult<Arc<NodeSnapshot>>`; `Snapshot::manifest_files()` yields
fallible items and `get_manifest_file` was removed. `Snapshot::is_initial` and
`Snapshot::INITIAL_SNAPSHOT_ID` were added. `ManifestSplits` is now
`ManifestSplits(pub Vec<Vec<u32>>)` and `from_extents` is gone.
`TransactionLog::{new_groups, new_arrays, deleted_groups, deleted_arrays,
updated_groups, updated_arrays}` became fallible iterators, and `moves`,
`has_moves`, `merge`, `updated_chunks_counts` were added.

### 19.8 `storage`, `config`, `ops`, `refs`, `store`

- `Storage::can_write` and `default_settings` became async and fallible;
  `root_is_clean` takes `&Settings`; `storage_info()` was added; the constructors
  now return `Arc<dyn Storage + Send + Sync>` (a `Arc<dyn Storage>` binding still
  compiles but no longer satisfies `Repository::create`).
  `storage::VersionInfo` itself is **unchanged**, `for_creation()` and
  `is_create()` included, even though it now guards the `repo` object as well as
  the first-generation ref files.
- `pub mod object_store` and `pub mod s3` were removed from `icechunk::storage`;
  `Reader`, `FetchConfigResult`, `UpdateConfigResult`, `GetRefResult` and
  `WriteRefResult` are no longer public there.
- `RepositoryConfig::set_virtual_chunk_container` returns `Result<(), String>`;
  new fields `repo_update_retries` and `num_updates_per_repo_info_file`; new
  `ManifestConfig::virtual_chunk_location_compression`; new
  `ManifestPreloadConfig::max_arrays_to_scan()`.
- `garbage_collect` and `expire` changed signature (§18.2); `expire_ref` and
  `ExpireRefResult` moved to `ops::expiration_v1`; `all_roots` split into
  `all_roots_v1` / `all_roots_v2`.
- `refs::fetch_branch_tip` → `fetch_branch_tip_v1`; `refs::list_deleted_tags` and
  `Ref::name` were added; every `refs::*` free function addresses
  **first-generation** on-disk refs only.
- `Store::from_bytes(bytes: &[u8])`; `ArrayMetadata` and `get_chunk_shapes` were
  added.

### 19.9 Toolchain

`edition` and `rust-version` moved to `[workspace.package]`
(`edition = "2024"`, `rust-version = "1.94.0"`). The workspace gained large
`[workspace.lints.rust]` and `[workspace.lints.clippy]` tables. Those are
warnings; the verifier does **not** build with `-D warnings` and does not check
lint cleanliness.

---

## 20. What is out of scope

Stated so the boundary is legible. None of the following is asserted anywhere in
the verifier.

**Out of scope entirely**

- **The JavaScript and WebAssembly bindings.** `icechunk-js` and the
  `@earthmover/icechunk` npm package are not built, not loaded and out of scope.
  What *is* in scope is the core refactoring that makes them possible: the
  workspace split of §1.1 and §2, and the `compat::IcechunkBoxFuture` shim in the
  chunk-reader and chunk-writer signatures. No test runs Node, `wasm-pack` or a
  browser, and no test asserts anything about a `.wasm` artifact, a
  `package.json`, or a napi binding.
- **Anything that needs a network, a cloud credential, a local daemon or a
  fault-injecting proxy.** S3, GCS, Azure, Tigris, R2 and MinIO; `new_s3_storage`
  and friends beyond their existence as names; HTTP storage
  (`new_http_storage`) and redirect storage (`new_redirect_storage`,
  `storage::redirect`), including the ten-hop limit and the recognised-scheme
  list; retry, timeout and stalled-stream *behaviour* (only the configuration
  surface is in scope); `storage::latency`.
- **The Python package.** `icechunk-python` is not built. Where the corpus
  documents a Python name with no Rust counterpart — `ForkSession`,
  `upgrade_icechunk_repository`, `Repository.manifest_files`,
  `Session.chunk_type` — the Rust surface named in this document is what counts.

**In the crate, but not asserted**

- The virtual-chunk-location zstd dictionary: `LocationCompressionConfig`,
  `ManifestVirtualChunkLocationCompressionConfig`,
  `Manifest::{uses_location_compression, location_dictionary_size,
  num_compressed_refs}`. Implement it; nothing measures a compressed manifest's
  size or its dictionary.
- The retry schedule of §4 — attempt counts, delays, jitter, and
  `RepoUpdateAttemptsLimit` itself.
- `total_snapshots` truncation in `AncestryGraph` (the 100-node display cap) and
  the glyphs, spacing and colour values of any rendering — the escape sequence
  and the hex string `palette_ansi` and `palette_hex` return included. Their
  *signatures* are in scope, and §2.4 states them.
- The naming scheme of a file under `overwritten/`, and the ops-log chain across
  more than one `repo` backup file (only the single-file case is asserted).
- `inspect::{snapshot_json, repo_info_json, manifest_json, transaction_log_json}`.
- `AncestryGraph::new` called directly, `LayoutElement`'s variants' contents.

**Required here, but not measurable from a driver that must compile**

These are real obligations — `instruction.md` and the sections above state them,
and a later milestone may depend on them — but no requirement group asserts any of
them, and none can, because the verifier reaches your crate only by compiling
against it and running the binary. Getting them wrong costs nothing *at this
step*; getting them wrong still hands the next milestone a broken workspace.

- The workspace's **membership and manifest values**: nine members (§1.1),
  `edition = "2024"` / `rust-version = "1.94.0"` (§1.1, §19.9), the `icechunk`
  package's own version `2.0.0` and the sibling crates' versions (§1.2), and
  `autotests = false` (§1.5). No driver reads a manifest.
- Every **absence** requirement. A driver that still *used* a removed path would
  not compile, so the verifier cannot assert one is gone; it only never names it.
  That covers twelve of §2.3's thirteen removed paths, the removal of
  `Session::{flush, rewrite_manifests, commit_rebasing}` (§6), the removal of the
  blanket `From<E> for ICError<Kind>` impls (§17) and of `SnapshotInfo::manifests`
  (§19.7), and `#[non_exhaustive]` on `RepositoryErrorKind`, `SessionErrorKind`,
  `MigrationErrorKind` and `VersionInfo` (§17). The one exception is
  `RepositoryErrorKind::RepositoryIsV2`: its removal is *behaviour* — a
  second-generation repository now opens (§16) — and that **is** in scope.
- `AssetManager::{fetch_repo_info_backup, backup_path_for_repo_info}` (§3). They
  exist for the ops-log chain across more than one `repo` backup file, which is
  out of scope above, and for the `overwritten/` naming scheme, which §3.3
  declines to assert.

**Never in scope, at any milestone**

- Internal module layout, private names, field order, file organisation.
- Log output, `tracing` spans, and the text of any message except the four
  `InvalidCommitConfiguration` reasons (§6) and the `"first commit"` substring
  (§6.1).
- Allocation strategy, timing, throughput, memory use.
- Your own tests, benchmarks, examples or CI configuration; lint cleanliness;
  rustfmt.
