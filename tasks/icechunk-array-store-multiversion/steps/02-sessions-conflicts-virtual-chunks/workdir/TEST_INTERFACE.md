# TEST_INTERFACE.md — milestone 02 (sessions, conflicts, virtual chunk containers)

This is the public contract the hidden verifier holds you to. Everything the
verifier names — crate identity, build command, module paths, exported symbols,
signatures, formats crossing the boundary, and the pinned crates it links
against — appears here or in a section of the specification corpus named from
`instruction.md`'s reading list. The verifier may combine these in ways this
document does not enumerate; it will not use an interface this document does not
name.

This document is **self-contained**. It restates milestone 01's contract *as
this release spells it*, so a session that starts here needs no earlier
document. Section 5 lists every milestone-01 name, path or signature that
changed; the new form is what is required and the old form is not accepted.

Section 9 lists what is **out of scope** at this milestone. Read it: several
things the corpus describes are deliberately outside this milestone's contract.

---

## 1. Crate identity and build contract

| Item | Value |
| --- | --- |
| Workspace manifest | `/app/Cargo.toml` — a Cargo **workspace**, `resolver = "2"` |
| Workspace members | `["icechunk"]` at minimum; `default-members = ["icechunk"]` |
| Package under test | `icechunk` at `/app/icechunk` |
| Package version | `0.1.0-alpha.8` (or `{ workspace = true }` resolving to it) |
| Crate root | `/app/icechunk/src/lib.rs` |
| Crate type | Rust library (`lib`), edition `2021` |
| Lockfile | `/app/Cargo.lock` — present, committed, consistent with the manifests |
| Build command | `cargo build` run in `/app` |
| Toolchain | the `rustc`/`cargo` in the verifier image; no nightly features |

The verifier runs `cargo build` in `/app` first. If that fails, every requirement
group of this milestone fails. Then it creates a
**throw-away driver crate outside `/app`** that declares
`icechunk = { path = "/app/icechunk" }` and compiles its own integration tests
against your crate. Your manifests are never modified and no test of yours is
ever run or read as evidence.

`cargo build` at `/app` must build the workspace's default members, and that
must be the `icechunk` library. If you add workspace members, keep
`default-members = ["icechunk"]` so a bare `cargo build` does not require
anything the verifier image cannot build.

The verifier image carries a pre-populated Cargo registry cache for this
release's dependency graph plus the driver crate's, and a task-owned lockfile
for the drivers. A candidate that leaves the manifests alone resolves nothing
and downloads nothing at check time. A dependency you add is fetched then, so
prefer crates already in `Cargo.lock`.

Milestone 01's behaviour is re-exercised through this release's API, alongside
this milestone's own capability.

## 2. Workspace layout

```text
/app/
  Cargo.toml            workspace manifest              (required)
  Cargo.lock            resolved graph                  (required)
  icechunk/
    Cargo.toml          package manifest, name = "icechunk"
    src/                your implementation (layout below src/ is yours)
  icechunk-spec/        the specification corpus (read-only)
  TEST_INTERFACE.md     this file
  WORKSPACE_HANDOFF.md  what to preserve when this milestone ends
  timer.sh              advisory countdown for this milestone's budget
```

Module organisation under `icechunk/src/` is yours **except** for the module
paths section 4 names: those are part of the public API and the drivers spell
them verbatim (`icechunk::session::Session`, and so on).

## 3. Crates the verifier links against

The driver crate declares exactly these dependencies and no others:

```toml
icechunk    = { path = "/app/icechunk" }
bytes       = { version = "1", features = ["serde"] }
chrono      = { version = "0.4", features = ["serde"] }
futures     = "0.3"
serde_json  = "1"
tempfile    = "3"
tokio       = { version = "1", features = ["rt-multi-thread", "macros", "fs", "sync", "time"] }
```

Consequences you must design for:

- **`bytes::Bytes`** crosses the boundary in both directions: store reads and
  writes, chunk readers and writers, and inline chunk payloads. Depend on a
  semver-compatible `bytes` 1.x, or the types are distinct and the drivers will
  not compile.
- **`tokio::sync::RwLock`** crosses the boundary: the store is constructed from
  an `Arc<tokio::sync::RwLock<Session>>` and hands the same handle back. It must
  be *that* `RwLock`, from a semver-compatible `tokio` 1.x — not
  `std::sync::RwLock`, not `parking_lot`, not one of your own.
- **`futures::Stream`** is what the streaming methods return. The drivers drive
  them with `futures::TryStreamExt` / `StreamExt`, so anything section 4 marks
  as a stream must implement `futures` 0.3's `Stream`.
- **`chrono::DateTime<chrono::Utc>`** crosses the boundary in snapshot metadata
  and in the garbage-collector configuration. Depend on `chrono` 0.4.x.
- **`serde_json::Value`** is what user attributes and codec configurations are
  built from, and snapshot properties are a `HashMap<String, serde_json::Value>`.
- The drivers run every asynchronous entry point on a plain tokio runtime,
  single-threaded **and** multi-threaded. Do not require a particular flavour;
  do not require the caller to install anything.
- The drivers use `tempfile::TempDir` for both repository roots and the files a
  virtual chunk reference points at, so the local-filesystem backend must accept
  an ordinary absolute directory path.
- The drivers `unwrap` your `Result`s and format failures with `{:?}`, so every
  error type must implement `std::fmt::Debug`, and the error enums section 8
  names must implement `std::error::Error` (`Display` included). Section 9
  excludes the **text** those produce, never the impls themselves.
- Where a case asserts that an operation **fails**, the driver reaches for
  `Result::expect_err` / `unwrap_err`, and those need the *success* type to
  implement `Debug` too. Section 4 carries a `// Debug` annotation on every type
  that reaches such a call — `Repository` and `refs::BranchVersion` included,
  not only the error types. Annotations of that form in section 4 are part of
  the contract: a type annotated `// Clone + Debug + PartialEq` must implement
  at least those traits.
- `object_store` is **not** a driver dependency, and nothing in your public API
  may require the caller to name an `object_store` type to use it.

## 4. The public API required at this milestone

This section is the complete contract, so it also names items the drivers never
call themselves — types a signature mentions, and the constructors section 5's
migration turns on. Those are listed so the contract is unambiguous; what is
actually exercised is the **Required behaviour** bullets under each subsection
and the categories of section 8. Section 9 says which of the rest is explicitly
out of scope.

The crate root:

```rust
pub mod change_set;
pub mod config;
pub mod conflicts;
pub mod format;
pub mod metadata;
pub mod ops;
pub mod refs;
pub mod repository;
pub mod session;
pub mod storage;
pub mod store;
pub mod virtual_chunks;

pub use config::{ObjectStoreConfig, RepositoryConfig};
pub use repository::Repository;
pub use storage::{
    new_in_memory_storage, new_local_filesystem_storage, new_s3_storage,
    MemCachingStorage, ObjectStorage, Storage, StorageError,
};
pub use store::Store;
```

Note two root names that now resolve somewhere new: `icechunk::RepositoryConfig`
is `config::RepositoryConfig` and `icechunk::Store` is `store::Store`. Section 5
has the full list.

### 4.1 `icechunk::storage` — constructing a backend

```rust
pub type ETag = String;

pub trait Storage: std::fmt::Debug + Sync + Send { /* sealed */ }

pub fn new_in_memory_storage() -> StorageResult<Arc<dyn Storage>>;
pub fn new_local_filesystem_storage(path: &std::path::Path) -> StorageResult<Arc<dyn Storage>>;
pub fn new_s3_storage(
    config: S3Options,
    bucket: String,
    prefix: Option<String>,
    credentials: Option<S3Credentials>,
) -> StorageResult<Arc<dyn Storage>>;

pub struct ObjectStorage { /* private */ }
impl ObjectStorage {
    pub fn new_in_memory() -> Result<ObjectStorage, StorageError>;
    pub fn new_local_filesystem(prefix: &std::path::Path) -> Result<ObjectStorage, StorageError>;
}
pub struct MemCachingStorage { /* private */ }
```

`Storage` has `Send + Sync` as supertraits, so an `Arc<dyn Storage>` returned by
a constructor must be assignable to an `Arc<dyn Storage + Send + Sync>` binding;
the drivers write exactly that coercion. `Storage` is **sealed** — the drivers
never implement it, and you need not let them.

The drivers build every repository with `new_in_memory_storage()` or
`new_local_filesystem_storage(<a temp dir>)`, and wrap one with
`Repository::add_in_mem_asset_caching`. `new_s3_storage` and `ObjectStorage`'s
own constructors are named here because section 5's migration renamed them; the
drivers do construct them — `new_in_memory_storage` and
`new_local_filesystem_storage` imported from `icechunk::storage`,
`new_s3_storage` and `ObjectStorage` from the crate root, so both paths of
section 5.2 are exercised — but no request is ever made to S3 (PROFILE P1) —
`new_s3_storage` is synchronous and only describes a backend. `MemCachingStorage`
is named for the same reason and the drivers never call it; only
`Repository::add_in_mem_asset_caching` is exercised.

### 4.2 `icechunk::repository`

```rust
pub enum VersionInfo {
    SnapshotId(SnapshotId),
    TagRef(String),
    BranchTipRef(String),
}                                       // Debug + Clone + PartialEq + Eq

pub struct Repository { /* private */ }        // Debug

impl Repository {
    pub async fn create(
        config: Option<RepositoryConfig>,
        storage: Arc<dyn Storage + Send + Sync>,
        virtual_chunk_credentials: HashMap<ContainerName, Credentials>,
    ) -> RepositoryResult<Self>;
    pub async fn open(/* same three arguments */) -> RepositoryResult<Self>;
    pub async fn open_or_create(/* same three arguments */) -> RepositoryResult<Self>;

    pub async fn exists(storage: &(dyn Storage + Send + Sync)) -> RepositoryResult<bool>;
    pub fn add_in_mem_asset_caching(
        storage: Arc<dyn Storage + Send + Sync>,
    ) -> Arc<dyn Storage + Send + Sync>;

    pub async fn fetch_config(
        storage: &(dyn Storage + Send + Sync),
    ) -> RepositoryResult<Option<(RepositoryConfig, ETag)>>;
    pub async fn save_config(&self) -> RepositoryResult<ETag>;
    pub fn config(&self) -> &RepositoryConfig;
    pub fn storage(&self) -> &Arc<dyn Storage + Send + Sync>;

    pub async fn ancestry(
        &self,
        snapshot_id: &SnapshotId,
    ) -> RepositoryResult<impl Stream<Item = RepositoryResult<SnapshotMetadata>>>;

    pub async fn create_branch(&self, branch_name: &str, snapshot_id: &SnapshotId)
        -> RepositoryResult<BranchVersion>;
    pub async fn list_branches(&self) -> RepositoryResult<HashSet<String>>;
    pub async fn lookup_branch(&self, branch: &str) -> RepositoryResult<SnapshotId>;
    pub async fn reset_branch(&self, branch: &str, snapshot_id: &SnapshotId)
        -> RepositoryResult<BranchVersion>;
    pub async fn delete_branch(&self, branch: &str) -> RepositoryResult<()>;

    pub async fn create_tag(&self, tag_name: &str, snapshot_id: &SnapshotId)
        -> RepositoryResult<()>;
    pub async fn list_tags(&self) -> RepositoryResult<HashSet<String>>;
    pub async fn lookup_tag(&self, tag: &str) -> RepositoryResult<SnapshotId>;

    pub async fn readonly_session(&self, version: &VersionInfo) -> RepositoryResult<Session>;
    pub async fn writable_session(&self, branch: &str) -> RepositoryResult<Session>;
}

pub type RepositoryResult<T> = Result<T, RepositoryError>;
```

Required behaviour:

- `create` refuses when the repository already exists (`AlreadyInitialized`); it
  writes an empty snapshot, points `main` at it, and writes the configuration
  object, whose ETag it keeps.
- `open` refuses when there is no repository at that location
  (`RepositoryDoesntExist`) — both when `main` is missing and when no
  configuration object is stored.
- `open_or_create` opens when the repository exists and creates otherwise.
- `exists` reports whether the default branch `main` is there.
- `add_in_mem_asset_caching` returns a wrapped storage that opens the same
  repository. How much it caches is out of scope.
- `reset_branch` refuses a snapshot id that names no stored snapshot
  (`SnapshotNotFound`), and otherwise repoints the branch head with a
  compare-and-set against the current tip. It may move a branch **backwards or
  forwards**; it deletes nothing.
- `readonly_session` resolves all three `VersionInfo` forms; a snapshot id that
  names no stored snapshot is refused.
- `writable_session(branch)` is anchored at that branch's tip. There is **no**
  way to obtain a writable session from an arbitrary snapshot.
- `ancestry(id)` yields `SnapshotMetadata` newest first, starting with `id`
  itself and ending at the repository's first snapshot.
- A tag is immutable: creating one that exists fails. A branch is mutable;
  `delete_branch` removes it.

### 4.3 `icechunk::session`

```rust
pub struct Session { /* private */ }               // Clone + Debug + Serialize + Deserialize

impl Session {
    pub fn from_bytes(bytes: Vec<u8>) -> SessionResult<Self>;
    pub fn as_bytes(&self) -> SessionResult<Vec<u8>>;

    pub fn branch(&self) -> Option<&str>;
    pub fn read_only(&self) -> bool;               // == branch().is_none()
    pub fn snapshot_id(&self) -> &SnapshotId;
    pub fn has_uncommitted_changes(&self) -> bool;
    pub fn changes(&self) -> &ChangeSet;
    pub fn config(&self) -> &RepositoryConfig;

    pub async fn add_group(&mut self, path: Path) -> SessionResult<()>;
    pub async fn delete_group(&mut self, path: Path) -> SessionResult<()>;
    pub async fn add_array(&mut self, path: Path, metadata: ZarrArrayMetadata) -> SessionResult<()>;
    pub async fn update_array(&mut self, path: Path, metadata: ZarrArrayMetadata) -> SessionResult<()>;
    pub async fn delete_array(&mut self, path: Path) -> SessionResult<()>;
    pub async fn set_user_attributes(&mut self, path: Path, atts: Option<UserAttributes>)
        -> SessionResult<()>;
    pub async fn set_chunk_ref(&mut self, path: Path, coord: ChunkIndices,
                               data: Option<ChunkPayload>) -> SessionResult<()>;

    pub async fn get_node(&self, path: &Path) -> SessionResult<NodeSnapshot>;
    pub async fn get_array(&self, path: &Path) -> SessionResult<NodeSnapshot>;
    pub async fn get_group(&self, path: &Path) -> SessionResult<NodeSnapshot>;
    pub async fn get_chunk_ref(&self, path: &Path, coords: &ChunkIndices)
        -> SessionResult<Option<ChunkPayload>>;
    pub async fn get_chunk_reader(&self, path: &Path, coords: &ChunkIndices,
                                  byte_range: &ByteRange)
        -> SessionResult<Option<Pin<Box<dyn Future<Output = SessionResult<Bytes>> + Send>>>>;
    pub fn get_chunk_writer(&self)
        -> impl FnOnce(Bytes) -> Pin<Box<dyn Future<Output = SessionResult<ChunkPayload>> + Send>>;

    pub async fn clear(&mut self) -> SessionResult<()>;
    pub async fn list_nodes(&self) -> SessionResult<impl Iterator<Item = NodeSnapshot> + '_>;
    pub async fn all_chunks(&self)
        -> SessionResult<impl Stream<Item = SessionResult<(Path, ChunkInfo)>> + '_>;
    pub async fn all_virtual_chunk_locations(&self)
        -> SessionResult<impl Stream<Item = SessionResult<String>> + '_>;

    pub fn discard_changes(&mut self) -> ChangeSet;
    pub async fn merge(&mut self, changes: ChangeSet) -> SessionResult<()>;
    pub async fn commit(&mut self, message: &str, properties: Option<SnapshotProperties>)
        -> SessionResult<SnapshotId>;
    pub async fn rebase(&mut self, solver: &dyn ConflictSolver) -> SessionResult<()>;
}

impl From<Session> for ChangeSet { /* … */ }   // so `session.into()` feeds `merge`

pub type SessionResult<T> = Result<T, SessionError>;

pub fn is_prefix_match(key: &str, prefix: &str) -> bool;
pub async fn get_chunk(
    reader: Option<Pin<Box<dyn Future<Output = SessionResult<Bytes>> + Send>>>,
) -> SessionResult<Option<Bytes>>;
pub fn construct_valid_byte_range(request: &ByteRange, chunk_offset: u64, chunk_length: u64)
    -> ByteRange;
```

Required behaviour:

- **`commit` checks the branch tip first.** If it is not the session's parent
  snapshot, the commit fails with the conflict category *before writing
  anything*, carrying `expected_parent` (the session's parent) and
  `actual_parent` (the branch tip). The branch does not move and the session
  keeps its changes and its branch.
- **A successful `commit` self-demotes the session.** It clears the change set,
  advances the session's snapshot to the new commit, and drops the branch, so
  the same session is read-only afterwards and cannot commit twice. It still
  *reads* what it committed.
- A commit with an empty change set is refused (`NoChangesToCommit`) and the
  branch does not move; the session is **not** demoted by a failed commit.
- `merge`, `commit` and `rebase` on a read-only session all report the read-only
  category. See PROFILE §D6 for where that boundary is and is not enforced.
- `set_chunk_ref` validates the coordinates against the array's shape and chunk
  shape, and refuses an out-of-range index (`InvalidIndex`). Passing `None` as
  the payload removes the reference.
- `get_chunk_writer()` inlines a payload of at most
  `config.inline_chunk_threshold_bytes` bytes and materialises anything larger
  (PROFILE P7).
- **`clear()` removes every node the session can see**, groups and arrays
  alike, so `list_nodes` yields nothing afterwards and the change set records
  the deletions like any other edit. It is milestone 01's `Repository::clear`
  under the move of section 5.3, and it is what `Store::clear` (section 4.7)
  reaches through the store.
- `rebase(solver)` walks the commits between the session's parent and the
  branch tip **oldest first**, asking the solver to resolve each. On a resolved
  commit it advances the session's snapshot to it; on an unresolvable one it
  restores the session's unmodified change set and fails with a report naming
  that commit and its conflicts — so a partial rebase leaves the session
  positioned at the last commit it did fast-forward over, ready to commit there.
  `rebase` is `Ok(())` when the branch tip already is the session's parent.
- `discard_changes` empties the session and returns the change set, so it can be
  merged elsewhere. `merge` folds a change set in **without** committing.
- `as_bytes` / `from_bytes` round-trip the whole session — its branch, its
  snapshot and its pending changes — so a session can be marshalled to a remote
  worker and its changes merged back.
- `all_virtual_chunk_locations` yields the location URL of every virtual chunk
  reference the session can see, and nothing for inline or materialised chunks.

The three free functions of this module carry the following semantics:

- `get_chunk(reader)` awaits the reader a `get_chunk_reader` handed back, so
  `None` stays `None` and a present reader yields its bytes.
- **`is_prefix_match(key, prefix)` matches on whole `/`-separated components,
  never on a shared text prefix of a component.** It is `true` when `prefix` is
  empty (every key matches), when `key` equals `prefix`, and when what remains
  after stripping `prefix` off `key` starts with `/`. It is `false` otherwise —
  in particular `is_prefix_match("array-suffix/zarr.json", "array")` is `false`,
  which a plain `starts_with` would get wrong. This is the rule the store's
  `list_prefix` filters keys with.
  The prefix `"/"` is the one exception to the stripping rule: it names the root
  (see section 4.7), so nothing is stripped and `is_prefix_match(key, "/")` is
  `true` exactly when `key` is empty or itself begins with `/`. Store keys never
  begin with `/`, so no in-scope behaviour depends on that case.
- **`construct_valid_byte_range(request, chunk_offset, chunk_length)` maps a
  request expressed in coordinates relative to a chunk onto that chunk's window
  inside the object holding it, and always returns a `ByteRange::Bounded` in the
  object's absolute coordinates.** Bounding the result is the point of the
  function: an unbounded result would read past the end of the chunk. Precisely,
  writing `end = chunk_offset + chunk_length` for the window's exclusive end and
  `last = end - 1` for the chunk's last byte:

  | `request` | result |
  | --- | --- |
  | `Bounded(s..e)` | `Bounded(min(chunk_offset + s, last) .. min(chunk_offset + e, end))` |
  | `From(n)` | `Bounded(min(chunk_offset + n, last) .. end)` |
  | `Last(n)` | `Bounded((end - n) .. end)` |

  `ByteRange::ALL` is `From(0)`, so it maps to the whole chunk window
  `Bounded(chunk_offset..end)`. A start that would fall past the chunk's last
  byte is clamped to it — that is the `min(.., last)` in the first two rows.
  The drivers only ever pass a request that lies inside the chunk, so
  `chunk_length` is always at least 1 and `s` and `n` never exceed it; what the
  function does with a request that starts before the chunk (`Last(n)` with
  `n > chunk_length`) is left open here and is out of scope (section 9).

### 4.4 `icechunk::conflicts`

```rust
// icechunk::conflicts
pub enum Conflict {                                        // Debug + PartialEq + Eq
    NewNodeConflictsWithExistingNode(Path),
    NewNodeInInvalidGroup(Path),
    ZarrMetadataDoubleUpdate(Path),
    ZarrMetadataUpdateOfDeletedArray(Path),
    UserAttributesDoubleUpdate { path: Path, node_id: NodeId },
    UserAttributesUpdateOfDeletedNode(Path),
    ChunkDoubleUpdate { path: Path, node_id: NodeId, chunk_coordinates: HashSet<ChunkIndices> },
    ChunksUpdatedInDeletedArray { path: Path, node_id: NodeId },
    ChunksUpdatedInUpdatedArray { path: Path, node_id: NodeId },
    DeleteOfUpdatedArray { path: Path, node_id: NodeId },
    DeleteOfUpdatedGroup { path: Path, node_id: NodeId },
}

pub enum ConflictResolution {                              // Debug
    Patched(ChangeSet),
    Unsolvable { reason: Vec<Conflict>, unmodified: ChangeSet },
}

#[async_trait]
pub trait ConflictSolver {
    async fn solve(
        &self,
        previous_change: &TransactionLog,
        previous_repo: &Session,
        current_changes: ChangeSet,
        current_repo: &Session,
    ) -> SessionResult<ConflictResolution>;
}

// icechunk::conflicts::detector
pub struct ConflictDetector;                               // Debug + Clone, unit struct
impl ConflictSolver for ConflictDetector { /* … */ }

// icechunk::conflicts::basic_solver
pub enum VersionSelection { Fail, UseOurs, UseTheirs }      // Debug + Clone + PartialEq + Eq
pub struct BasicConflictSolver {                            // Debug + Clone
    pub on_user_attributes_conflict: VersionSelection,
    pub on_chunk_conflict: VersionSelection,
    pub fail_on_delete_of_updated_array: bool,
    pub fail_on_delete_of_updated_group: bool,
}
impl Default for BasicConflictSolver { /* UseOurs, UseOurs, false, false */ }
impl ConflictSolver for BasicConflictSolver { /* … */ }
```

The two node-creation variants carry **different** paths, and the difference is
in scope. `NewNodeConflictsWithExistingNode(Path)` carries the path of the node
being created, which the intervening commit already created. `NewNodeInInvalidGroup(Path)`
carries the path of the **ancestor that is not a group** — the node the
intervening commit made an array — not the path being created underneath it. So
creating `/a/b/inner` after they made `/a/b` an array reports
`NewNodeInInvalidGroup("/a/b")`.

`Conflict` is constructed by the drivers and compared with `==`, and
`BasicConflictSolver` is built with struct-update syntax
(`BasicConflictSolver { on_chunk_conflict: …, ..Default::default() }`), so its
four fields are public and its `Default` is exactly the values above.

Which class is repairable, and by which knob:

| `Conflict` variant | Repairable? |
| --- | --- |
| `ChunkDoubleUpdate` | yes, unless `on_chunk_conflict == Fail`. `UseOurs` keeps our chunk; `UseTheirs` drops ours for those coordinates |
| `UserAttributesDoubleUpdate` | yes, unless `on_user_attributes_conflict == Fail`. `UseTheirs` **undoes** our update, so a session whose only change was that update has nothing left to commit |
| `DeleteOfUpdatedArray` | yes, unless `fail_on_delete_of_updated_array`; the delete wins |
| `DeleteOfUpdatedGroup` | yes, unless `fail_on_delete_of_updated_group`; the delete wins |
| every other variant | **never** — no configuration of `BasicConflictSolver` repairs it |

`ConflictDetector` never repairs anything: if any conflict is present it always
fails, including one `BasicConflictSolver` would have repaired.

### 4.5 `icechunk::config`

```rust
pub struct RepositoryConfig {                     // Clone + Debug + PartialEq + Eq + serde
    pub inline_chunk_threshold_bytes: u16,
    pub unsafe_overwrite_refs: bool,
    pub get_partial_values_concurrency: u16,
    pub virtual_chunk_containers: HashMap<ContainerName, VirtualChunkContainer>,
}

impl Default for RepositoryConfig {
    // inline_chunk_threshold_bytes: 512
    // unsafe_overwrite_refs: false
    // get_partial_values_concurrency: 10
    // virtual_chunk_containers: the five default containers of PROFILE §D5
}

impl RepositoryConfig {
    pub fn set_virtual_chunk_container(&mut self, cont: VirtualChunkContainer);
    pub fn virtual_chunk_containers(&self) -> impl Iterator<Item = &VirtualChunkContainer>;
    pub fn clear_virtual_chunk_containers(&mut self);
}

pub struct S3Options {                            // Clone + Debug + PartialEq + Eq + serde
    pub region: Option<String>,
    pub endpoint_url: Option<String>,
    pub anonymous: bool,
    pub allow_http: bool,
}

pub enum ObjectStoreConfig {                      // Clone + Debug + PartialEq + Eq + serde
    InMemory,
    LocalFileSystem(PathBuf),
    S3Compatible(S3Options),
    S3(S3Options),
    Gcs(HashMap<String, String>),
    Azure {},
    Tigris {},
}

pub struct S3StaticCredentials {                  // Clone + Debug + PartialEq + Eq + serde
    pub access_key_id: String,
    pub secret_access_key: String,
    pub session_token: Option<String>,
    pub expires_after: Option<DateTime<Utc>>,
}

pub enum S3Credentials { FromEnv, Anonymous, Static(S3StaticCredentials),
                         Refreshable(Arc<dyn CredentialsFetcher>) }   // Clone + Debug + Default
pub enum GcsStaticCredentials { ServiceAccount(PathBuf), ServiceAccountKey(String),
                                ApplicationCredentials(PathBuf) };
pub enum GcsCredentials { FromEnv, Static(GcsStaticCredentials) };
pub enum Credentials { S3(S3Credentials), Gcs(GcsCredentials) }       // Clone + Debug, NOT PartialEq

#[async_trait]
pub trait CredentialsFetcher: std::fmt::Debug + Sync + Send {
    async fn get(&self) -> Result<S3StaticCredentials, String>;
}
```

`RepositoryConfig` is `PartialEq + Eq`, so the drivers compare a whole config
with `==`, and it is built with struct-update syntax
(`RepositoryConfig { inline_chunk_threshold_bytes: 42, ..Default::default() }`),
so all four fields are public. There are **no** `with_*` chaining builders.

Configuration persistence, from `design/001` → "Global config":

- Exactly one configuration object per repository, global across all snapshots.
- Creating a repository writes it and yields an `ETag`.
- `Repository::fetch_config(storage)` reads it back as a `RepositoryConfig` plus
  its `ETag`, or `None` when there is none.
- `save_config` writes the instance's configuration **conditionally**, against
  the ETag that instance read. A save from an instance whose view has since been
  superseded is refused with the config-conflict category and changes nothing.
- The override is wholesale, not per field — PROFILE §D3.
- The on-disk encoding of the configuration object is out of scope (PROFILE P3);
  only that `fetch_config` reads back what `save_config` wrote.

### 4.6 `icechunk::virtual_chunks`

```rust
pub type ContainerName = String;

pub struct VirtualChunkContainer {                // Clone + Debug + PartialEq + Eq + serde
    pub name: ContainerName,
    pub url_prefix: String,
    pub store: ObjectStoreConfig,
}

pub type VirtualChunkCredentialsError = String;
impl VirtualChunkContainer {
    pub fn validate_credentials(&self, cred: &Credentials)
        -> Result<(), VirtualChunkCredentialsError>;
}

pub fn mk_default_containers() -> HashMap<ContainerName, VirtualChunkContainer>;

// `VirtualReferenceError` below is `icechunk::format::manifest::VirtualReferenceError`
// — section 4.8 declares it; this module does not re-export it.

#[async_trait]
pub trait ChunkFetcher: std::fmt::Debug + Send + Sync {   /* sealed */
    async fn fetch_chunk(&self, chunk_location: &str, range: &ByteRange,
                         checksum: Option<&Checksum>) -> Result<Bytes, VirtualReferenceError>;
}

pub struct VirtualChunkResolver { /* private */ }
impl VirtualChunkResolver {
    pub fn new(containers: impl Iterator<Item = VirtualChunkContainer>,
               credentials: HashMap<ContainerName, Credentials>) -> Self;
    pub async fn get_fetcher(&self, chunk_location: &str)
        -> Result<Arc<dyn ChunkFetcher>, VirtualReferenceError>;
    pub async fn fetch_chunk(&self, chunk_location: &str, range: &ByteRange,
                             checksum: Option<&Checksum>) -> Result<Bytes, VirtualReferenceError>;
}

pub struct S3Fetcher { /* private */ }
pub struct LocalFSFetcher { /* private */ }
```

Required behaviour (see PROFILE §D4 and §D5 for the two drifts):

- Every repository is created with the five default containers named and
  prefixed `s3`, `gcs`, `az`, `tigris`, `file`.
- A container is chosen by sorting all containers by URL-prefix length
  descending and taking the first whose prefix the chunk location starts with.
- No matching container is a read-time failure with the no-container category.
  **Storing** a reference is never refused for want of a container.
- Credentials are supplied per container **name** when the repository is created
  or opened, and are validated then. A container backed by the local filesystem
  or by an in-memory store accepts **no** credentials at all, and supplying any
  makes `create`/`open` fail. An S3-backed container accepts S3 credentials and a
  GCS-backed one accepts GCS credentials; a mismatched pair fails. Credentials
  for a container name the configuration does not have are ignored — `create`
  and `open` succeed and the entry has no effect.
- The local-filesystem fetcher must read a `file://<absolute path>` location,
  honouring the reference's `offset` and `length` and the request's byte range.
  A virtual chunk location is **absolute**, so the `PathBuf` inside a container's
  `ObjectStoreConfig::LocalFileSystem(..)` is **not** a root the location is
  resolved against and plays no part in the read: the drivers build such
  containers with `PathBuf::new()` and expect an absolute `file://` location to
  resolve regardless. What restricts which locations a container serves is its
  `url_prefix`, nothing else.
- **Either checksum kind may be recorded on any virtual reference**, whatever
  backs the container, and the rule is the same for both: the read is served
  when the referenced object still satisfies the recorded checksum and refused
  with the object-modified category when it does not. For a local-filesystem
  object that means its own last-modified time must be no later than a recorded
  `LastModified`, and its own entity tag must equal a recorded `ETag`. Only the
  checksum's *relation* to the object is observed, never an absolute time
  (PROFILE §P5), and a reference with no checksum always fetches.
- Storing a virtual reference is **never** validated against a container at
  write time (PROFILE §D4 and section 9); the container is resolved on the read.

### 4.7 `icechunk::store` — the Zarr-facing key/value view

```rust
pub enum ListDirItem { Key(String), Prefix(String) }   // Debug + Clone + PartialEq + Eq + Hash
pub struct Store { /* private */ }                     // Clone

impl Store {
    pub async fn from_session(session: Arc<RwLock<Session>>) -> Self;
    pub fn from_session_and_config(session: Arc<RwLock<Session>>,
                                   get_partial_values_concurrency: u16) -> Self;
    pub fn from_bytes(bytes: Bytes) -> StoreResult<Self>;
    pub async fn as_bytes(&self) -> StoreResult<Bytes>;
    pub fn session(&self) -> Arc<RwLock<Session>>;
    pub async fn read_only(&self) -> bool;

    pub async fn is_empty(&self, prefix: &str) -> StoreResult<bool>;
    pub async fn clear(&self) -> StoreResult<()>;
    pub async fn get(&self, key: &str, byte_range: &ByteRange) -> StoreResult<Bytes>;
    pub async fn get_partial_values(
        &self, key_ranges: impl IntoIterator<Item = (String, ByteRange)>,
    ) -> StoreResult<Vec<StoreResult<Bytes>>>;
    pub async fn exists(&self, key: &str) -> StoreResult<bool>;

    pub fn supports_writes(&self) -> StoreResult<bool>;           // true
    pub fn supports_deletes(&self) -> StoreResult<bool>;          // true
    pub fn supports_listing(&self) -> StoreResult<bool>;          // true
    pub fn supports_partial_writes(&self) -> StoreResult<bool>;   // false

    pub async fn set(&self, key: &str, value: Bytes) -> StoreResult<()>;
    pub async fn set_if_not_exists(&self, key: &str, value: Bytes) -> StoreResult<()>;
    pub async fn set_virtual_ref(&self, key: &str, reference: VirtualChunkRef) -> StoreResult<()>;
    pub async fn delete(&self, key: &str) -> StoreResult<()>;
    pub async fn set_partial_values(
        &self, key_start_values: impl IntoIterator<Item = (&str, ChunkOffset, Bytes)>,
    ) -> StoreResult<()>;                                         // always the unimplemented category

    pub async fn list(&self) -> StoreResult<impl Stream<Item = StoreResult<String>> + Send>;
    pub async fn list_prefix(&self, prefix: &str)
        -> StoreResult<impl Stream<Item = StoreResult<String>> + Send>;
    pub async fn list_dir(&self, prefix: &str)
        -> StoreResult<impl Stream<Item = StoreResult<String>> + Send>;
    pub async fn list_dir_items(&self, prefix: &str)
        -> StoreResult<impl Stream<Item = StoreResult<ListDirItem>> + Send>;
}

pub type StoreResult<A> = Result<A, StoreError>;
```

Required behaviour:

- A store is a **view over one session**, not a copy: writes through the store
  are visible on the session it was built from, `session()` hands that same
  handle back, and committing through it is what advances the branch.
- `from_session` takes the partial-values concurrency from the session's
  configuration; `from_session_and_config` pins it explicitly.
- `set`, `set_if_not_exists`, `set_virtual_ref` and `delete` refuse on a
  read-only store with the read-only category; reads still work.
- `set_virtual_ref` accepts only a **chunk** key. A metadata key or a Zarr-v2
  key is refused with the not-allowed category, and nothing is stored.
- `get_partial_values` returns one result per requested key range, **in the
  order the ranges were given**, with a per-key `Result` so that a missing key
  is one failed entry rather than a failed call.
- **A read is a lookup, not a bounds check.** `get` reports the not-found
  category — and `get_partial_values` the same, in that key's own entry — for
  any well-formed key that holds nothing, **including a chunk key whose
  coordinates lie outside the array's shape**, such as `array/c/0/0/9` on a
  4x4x4 array; `exists` answers `false` for the same keys. Coordinate validation
  belongs to writing (`set_chunk_ref`, section 4.3): a coordinate that was never
  written is simply absent, not an index error. A key matching **none** of the
  forms in the grammar table below does not parse at all and is refused with the
  invalid-key category instead.
- `delete` of an absent key is not an error.
- `clear` removes every node; `is_empty(prefix)` reports whether anything is
  listed under that prefix.
- **What the four listing methods yield.** `list()` and `list_prefix(prefix)`
  yield **whole keys** — exactly the strings `get` accepts — with `list()`
  equivalent to listing the root. A key belongs to `prefix` by the
  component-wise rule of `session::is_prefix_match`, so `list_prefix("array")`
  and `list_prefix("array/")` both select `array/zarr.json` and neither selects
  `array-suffix/zarr.json`.
- **`list_dir(prefix)` and `list_dir_items(prefix)` report one level**: each item
  is the single path component immediately below `prefix`, **relative** to it,
  with no trailing slash and no duplicates. `list_dir_items` classifies each as
  `ListDirItem::Key(name)` when that component is the whole of a key and
  `ListDirItem::Prefix(name)` when more key follows below it; `list_dir` yields
  the same names as plain strings, whichever kind they are. So a store holding
  `zarr.json`, `array/zarr.json` and `array/c/0/1/0` reports `Key("zarr.json")`
  and `Prefix("array")` at the root, and `"0"` for the prefix `array/c/`.
  **The order in which any of the four listing methods yields its items is
  free** (PROFILE §P4). The drivers sort every listing before comparing it, and
  the one that collects `ListDirItem`s sorts by a key it spells out itself, so
  nothing here requires any particular `Ord` on `ListDirItem`.
- **Prefix normalisation for listing.** A trailing `/` on the prefix is ignored,
  and the prefix `"/"` names the **root**, exactly as `""` does — Zarr clients
  spell the root both ways, so `list_dir("/")` and `list_dir("")` must report
  the same names, and neither may prepend a `/` to what it yields.
- `as_bytes` / `from_bytes` round-trip the whole underlying session through
  MessagePack — this is the path a Python pickle takes. The revived store has
  whatever read-only state the session had, so a store pickled after its session
  committed comes back **read-only** (the commit demoted it), while one pickled
  with uncommitted changes comes back writable, at the same snapshot, with those
  changes, and can still commit.

Key grammar, unchanged from milestone 01:

| Key | Meaning |
| --- | --- |
| `zarr.json` | metadata of the root group |
| `<path>/zarr.json` | metadata of the group or array at `/<path>` |
| `<path>/c/<i>/<j>/…` | the chunk of array `/<path>` at those indices |
| `.zgroup`, `.zarray`, `.zattrs`, `.zmetadata`, and `<path>/` forms of each | Zarr v2 keys; reads report not-found, writes report unimplemented |

A group metadata document and an array metadata document are told apart by
their content, not by the key. Writing an array metadata document at a key
creates an array there; writing a group metadata document creates a group.

### 4.8 `icechunk::format`, `icechunk::metadata`, `icechunk::change_set`, `icechunk::refs`

The milestone-01 data model, as this release spells it.

```rust
// icechunk::format
pub struct Path(/* private */);                    // Clone + Debug + PartialEq + Eq + Hash + Ord
impl TryFrom<&str>  for Path { type Error = PathError; }
impl TryFrom<String> for Path { /* … */ }
impl Path { pub fn root() -> Path; pub fn new(path: &str) -> Result<Path, PathError>; }
impl Display for Path { /* "/a/b" */ }

pub struct ChunkIndices(pub Vec<u32>);             // Clone + Debug + PartialEq + Eq + Hash + Ord
pub type ChunkOffset = u64;
pub type ChunkLength = u64;
pub type IcechunkFormatVersion = u16;

pub enum ByteRange {                               // Clone + Debug + PartialEq + Eq
    Bounded(Range<ChunkOffset>),
    From(ChunkOffset),
    Last(ChunkLength),
}
impl ByteRange {
    pub const ALL: Self;                           // == From(0)
    pub fn bounded(start: ChunkOffset, end: ChunkOffset) -> Self;
    pub fn from_offset(offset: ChunkOffset) -> Self;
    pub fn from_offset_with_length(offset: ChunkOffset, length: ChunkOffset) -> Self;
    pub fn to_offset(offset: ChunkOffset) -> Self;
    pub fn slice(&self, bytes: Bytes) -> Bytes;
}
impl From<Range<ChunkOffset>> for ByteRange { /* … */ }

pub struct ObjectId<const SIZE: usize, T>(pub [u8; SIZE], …);
pub type SnapshotId = ObjectId<12, SnapshotTag>;   // Clone + Debug + PartialEq + Eq + Hash + Ord
pub type ManifestId = ObjectId<12, ManifestTag>;
pub type ChunkId    = ObjectId<12, ChunkTag>;
pub type NodeId     = ObjectId<8,  NodeTag>;       // Clone, NOT Copy
impl<…> ObjectId<SIZE, T> { pub fn random() -> Self; }

// icechunk::format::manifest
pub enum VirtualReferenceError { /* the variants of section 8 */ }  // Debug + Error
pub struct VirtualChunkLocation(pub String);       // Clone + Debug + PartialEq + Eq + Hash + Ord
impl VirtualChunkLocation {
    pub fn from_absolute_path(path: &str) -> Result<Self, VirtualReferenceError>;
}
pub struct SecondsSinceEpoch(pub u32);             // Clone + Debug + PartialEq + Eq + Hash + Ord
pub enum Checksum { LastModified(SecondsSinceEpoch), ETag(String) }
                                                   // Clone + Debug + PartialEq + Eq + Hash + Ord
pub struct VirtualChunkRef {                       // Clone + Debug + PartialEq + Eq + Hash + Ord
    pub location: VirtualChunkLocation,
    pub offset: ChunkOffset,
    pub length: ChunkLength,
    pub checksum: Option<Checksum>,
}
pub struct ChunkRef { pub id: ChunkId, pub offset: ChunkOffset, pub length: ChunkLength }
                                                   // Clone + Debug + PartialEq + Eq + Hash + Ord
pub enum ChunkPayload { Inline(Bytes), Virtual(VirtualChunkRef), Ref(ChunkRef) }
                                                   // Clone + Debug + PartialEq + Eq + Hash + Ord
pub struct ChunkInfo { pub node: NodeId, pub coord: ChunkIndices, pub payload: ChunkPayload }
                                                   // Clone + Debug + PartialEq + Eq + Hash + Ord
pub struct Manifest { /* … */ }

// icechunk::format::snapshot
pub struct ZarrArrayMetadata {                     // Clone + Debug + PartialEq + serde
    pub shape: ArrayShape,                         // = Vec<u64>
    pub data_type: DataType,
    pub chunk_shape: ChunkShape,
    pub chunk_key_encoding: ChunkKeyEncoding,
    pub fill_value: FillValue,
    pub codecs: Vec<Codec>,
    pub storage_transformers: Option<Vec<StorageTransformer>>,
    pub dimension_names: Option<DimensionNames>,
}
impl ZarrArrayMetadata { pub fn valid_chunk_coord(&self, coord: &ChunkIndices) -> bool; }

pub enum NodeType { Group, Array }                 // Clone + Debug + PartialEq + Eq
pub enum NodeData { Array(ZarrArrayMetadata, Vec<ManifestRef>), Group }
                                                   // Clone + Debug + PartialEq
pub enum UserAttributesSnapshot { Inline(UserAttributes), Ref(UserAttributesRef) }
                                                   // Clone + Debug + PartialEq + Eq
pub struct NodeSnapshot {                          // Clone + Debug + PartialEq
    pub id: NodeId,
    pub path: Path,
    pub user_attributes: Option<UserAttributesSnapshot>,
    pub node_data: NodeData,
}
impl NodeSnapshot { pub fn node_type(&self) -> NodeType; }

pub struct SnapshotMetadata {                      // Clone + Debug + PartialEq + Eq
    pub id: SnapshotId,
    pub written_at: DateTime<Utc>,
    pub message: String,
}
pub type SnapshotProperties = HashMap<String, serde_json::Value>;
pub struct Snapshot { /* … */ }

// icechunk::format::transaction_log  (NEW at this milestone)
pub struct TransactionLog {                        // Clone + Debug + PartialEq + Default + serde
    pub icechunk_transaction_log_format_version: IcechunkFormatVersion,
    pub new_groups: HashSet<NodeId>,
    pub new_arrays: HashSet<NodeId>,
    pub deleted_groups: HashSet<NodeId>,
    pub deleted_arrays: HashSet<NodeId>,
    pub updated_user_attributes: HashSet<NodeId>,
    pub updated_zarr_metadata: HashSet<NodeId>,
    pub updated_chunks: HashMap<NodeId, HashSet<ChunkIndices>>,
}

// icechunk::metadata
pub type ArrayShape = Vec<u64>;
pub type DimensionName = Option<String>;
pub type DimensionNames = Vec<DimensionName>;
pub struct ChunkShape(pub Vec<NonZeroU64>);
pub enum ChunkKeyEncoding { Slash, Dot, Default }
pub enum DataType { Bool, Int8, Int16, Int32, Int64, UInt8, UInt16, UInt32, UInt64,
                    Float16, Float32, Float64, Complex64, Complex128, String, Bytes }
pub enum FillValue { Bool(bool), Int8(i8), …, Float64(f64), Complex64(f32, f32),
                     Complex128(f64, f64), String(String), Bytes(Vec<u8>) }
pub struct Codec { pub name: String, pub configuration: Option<serde_json::Value> }
pub struct StorageTransformer { pub name: String, pub configuration: Option<serde_json::Value> }
pub struct UserAttributes { pub parsed: serde_json::Value }
                                                   // Clone + Debug + PartialEq + Eq + serde
impl UserAttributes {
    pub fn try_new(json: &[u8]) -> Result<UserAttributes, serde_json::Error>;
    pub fn to_bytes(&self) -> Bytes;   // the JSON bytes `try_new` accepts, round-tripping
}

// icechunk::change_set
pub struct ChangeSet { /* private */ }             // Clone + Debug + Default + PartialEq + serde
impl ChangeSet {
    pub fn is_empty(&self) -> bool;
    pub fn merge(&mut self, other: ChangeSet);
    pub fn export_to_bytes(&self) -> SessionResult<Vec<u8>>;
    pub fn import_from_bytes(bytes: &[u8]) -> SessionResult<Self>;
}

// icechunk::refs
pub struct BranchVersion(pub u64);                 // Clone + Debug + PartialEq + Eq
pub enum Ref { Tag(String), Branch(String) }       // Clone + Debug + PartialEq + Eq + Hash
impl Ref { pub const DEFAULT_BRANCH: &'static str; }  // "main"
```

`Path` rejects a relative or non-canonical path, so `"/a/b".try_into()` succeeds
and `"a/b".try_into()` does not. `ChunkIndices(vec![])` is the single chunk of a
zero-dimensional array.

### 4.9 `icechunk::ops::gc`

```rust
pub enum Action { Keep, DeleteIfCreatedBefore(DateTime<Utc>) }      // Debug + PartialEq + Eq
pub struct GCConfig { /* private */ }                               // Debug
impl GCConfig {
    pub fn new(extra_roots: HashSet<SnapshotId>, dangling_chunks: Action,
               dangling_manifests: Action, dangling_attributes: Action,
               dangling_transaction_logs: Action, dangling_snapshots: Action) -> Self;
    pub fn clean_all(chunks_age: DateTime<Utc>, metadata_age: DateTime<Utc>,
                     extra_roots: Option<HashSet<SnapshotId>>) -> Self;
    pub fn deletes_chunks(&self) -> bool;
    pub fn deletes_manifests(&self) -> bool;
    pub fn deletes_attributes(&self) -> bool;
    pub fn deletes_transaction_logs(&self) -> bool;
    pub fn deletes_snapshots(&self) -> bool;
}
pub struct GCSummary {                                              // Debug + PartialEq + Eq + Default
    pub chunks_deleted: usize,
    pub manifests_deleted: usize,
    pub snapshots_deleted: usize,
    pub attributes_deleted: usize,
    pub transaction_logs_deleted: usize,
}
pub type GCResult<A> = Result<A, GCError>;
pub async fn garbage_collect(storage: &(dyn Storage + Send + Sync), config: &GCConfig)
    -> GCResult<GCSummary>;
```

Only three properties are required, and they are the ones every correct
collector has: a configuration that keeps everything reports so and deletes
nothing; a cut-off older than every object deletes nothing; and **anything
reachable from a reference survives a sweep with any cut-off**. Which unreachable
objects a sweep does collect is out of scope at this milestone (section 9).

## 5. Migration from milestone 01

Milestone 01's contract changes in the following ways. The new form is what is
required; the old form is not accepted.

### 5.1 Modules

| Milestone 01 | This milestone |
| --- | --- |
| `icechunk::zarr` | **gone.** The store moved to `icechunk::store`; the configuration types moved to `icechunk::config`; `VersionInfo` moved to `icechunk::repository` |
| `icechunk::storage::virtual_ref` | **gone** → `icechunk::virtual_chunks` |
| — | new: `config`, `conflicts`, `ops`, `session`, `store`, `virtual_chunks`, `format::transaction_log` |
| `change_set`, `format`, `metadata`, `refs`, `repository`, `storage` | unchanged paths |

`icechunk::repository` used to re-export `ChangeSet`, `ChunkPayload`,
`VirtualChunkLocation`, `SnapshotMetadata`, `ZarrArrayMetadata`, `ChunkIndices`,
`Path` and the whole `metadata::*` set. **It re-exports none of them now** —
each is reached through its own module, as section 4.8 spells it.

### 5.2 Crate-root re-exports

| Milestone 01 | This milestone |
| --- | --- |
| `icechunk::Store` = `zarr::Store` | `icechunk::Store` = `store::Store` — **the same name, a different type** |
| `icechunk::RepositoryConfig` = `repository::RepositoryConfig` | `icechunk::RepositoryConfig` = `config::RepositoryConfig` — **the same name, a different type with different fields** |
| `icechunk::RepositoryBuilder` | **removed**, with no replacement |
| `icechunk::SnapshotMetadata` | **removed** from the root; only `icechunk::format::snapshot::SnapshotMetadata` |
| `icechunk::{MemCachingStorage, ObjectStorage, Storage, StorageError}` | unchanged |
| — | added: `icechunk::ObjectStoreConfig`, `new_in_memory_storage`, `new_local_filesystem_storage`, `new_s3_storage` |

### 5.3 The data API moved from `Repository` to `Session`

A repository is no longer the unit of work. **A session is**, and two sessions
on one branch may conflict.

| Milestone 01 `Repository` item | This milestone |
| --- | --- |
| `Repository::init(storage, unsafe_overwrite_refs) -> RepositoryBuilder` | `Repository::create(Option<RepositoryConfig>, Arc<dyn Storage + Send + Sync>, HashMap<ContainerName, Credentials>)` |
| `Repository::update(storage, snapshot_id) -> RepositoryBuilder` | **removed** — a writable session comes only from a branch |
| `Repository::from_branch_tip(storage, branch)` | `Repository::open(..)` then `repo.writable_session(branch)` |
| `Repository::from_tag(storage, tag)` | `repo.readonly_session(&VersionInfo::TagRef(tag.into()))` |
| `RepositoryBuilder::{new, with_inline_threshold_bytes, with_unsafe_overwrite_refs, with_config, with_virtual_ref_config, with_change_set, build}` | **all removed** |
| `Repository::{snapshot_id, has_uncommitted_changes}` | `Session::{snapshot_id, has_uncommitted_changes}` |
| `Repository::{add_group, delete_group, add_array, update_array, delete_array, set_user_attributes, set_chunk_ref}` | `Session::*`, same names, `RepositoryResult` → `SessionResult` |
| `Repository::{get_node, get_array, get_group, get_chunk_ref, get_chunk_reader, get_chunk_writer, clear, list_nodes, all_chunks}` | `Session::*`, same names, `RepositoryResult` → `SessionResult` |
| `Repository::flush(..)` | **removed** (private inside the commit) |
| `Repository::{distributed_flush, distributed_commit}` | **removed** → `Session::merge(ChangeSet)` then `Session::commit` |
| `Repository::commit(&mut self, ..)` | `Session::commit(&mut self, message: &str, properties: Option<SnapshotProperties>) -> SessionResult<SnapshotId>` |
| `Repository::change_set_bytes(&self) -> RepositoryResult<Vec<u8>>` | `Session::as_bytes(&self) -> SessionResult<Vec<u8>>` — **the whole session, not just the change set** |
| `Repository::new_branch(&self, branch_name)` | `Repository::create_branch(&self, branch_name, snapshot_id)` — **an explicit snapshot id is now required** |
| `Repository::tag(&self, tag_name, snapshot_id)` | `Repository::create_tag(&self, tag_name, snapshot_id) -> RepositoryResult<()>` |
| `Repository::ancestry(&self)` (no argument) | `Repository::ancestry(&self, snapshot_id)` — **now takes the snapshot id** |
| `impl From<Repository> for ChangeSet` | `impl From<Session> for ChangeSet` |
| `icechunk::repository::get_chunk` | `icechunk::session::get_chunk` |

New on `Repository`: `open`, `open_or_create`, `fetch_config`, `save_config`,
`config`, `list_branches`, `lookup_branch`, `reset_branch`, `delete_branch`,
`list_tags`, `lookup_tag`, `readonly_session`, `writable_session`.

### 5.4 `RepositoryConfig` — two different types, one name

Milestone 01 had **two** types called `RepositoryConfig`. Both are gone.

- `repository::RepositoryConfig { inline_chunk_threshold_bytes, unsafe_overwrite_refs }`,
  a plain struct with no serde and no `Eq`;
- `zarr::RepositoryConfig { version, inline_chunk_threshold_bytes, unsafe_overwrite_refs,
  change_set_bytes, virtual_ref_config }` with `new`, `existing`, six `with_*`
  builders and `make_repository` — **removed entirely**.

This milestone's `config::RepositoryConfig` is section 4.5: four public fields,
`Serialize + Deserialize + PartialEq + Eq`, struct-update construction, and only
the three `virtual_chunk_container` methods.

### 5.5 `Store` — `zarr::Store` → `store::Store`

Removed from the store, with no replacement on it: `from_consolidated`,
`from_json`, `new_from_storage`, `from_repository`, `with_access_mode`,
`access_mode`, `current_branch`, `snapshot_id`, `current_version`,
`has_uncommitted_changes`, `reset`, `checkout`, `new_branch`, `commit`,
`distributed_commit`, `tag`, `ancestry`, `change_set_bytes`, `empty`. Version
control lives on `Repository` and `Session` now.

| Milestone 01 | This milestone |
| --- | --- |
| `Store::empty()` | `Store::is_empty(prefix)` |
| `Store::access_mode()` | `Store::read_only().await` |
| `Store::commit(msg)` | `store.session().write().await.commit(msg, None)` |
| `Store::clear(&mut self)` | `Store::clear(&self)` |
| `Store::list_dir` (keys only) | plus the new `Store::list_dir_items` |
| — | new: `from_session`, `from_session_and_config`, `from_bytes`, `as_bytes`, `session`, `read_only`, `is_empty` |

Removed types from `zarr`: `AccessMode`, `ConsolidatedStore`, `StoreOptions`,
`StorageConfig` (with `make_storage` / `make_cached_storage`), and
`zarr::RepositoryConfig`. `zarr::VersionInfo` is now
`repository::VersionInfo`, structurally unchanged.

### 5.6 Storage constructors

| Milestone 01 | This milestone |
| --- | --- |
| `ObjectStorage::new_in_memory_store(Option<String>) -> ObjectStorage` | `ObjectStorage::new_in_memory() -> Result<_, StorageError>` — **no prefix argument, now fallible** — or the free `new_in_memory_storage()` |
| `ObjectStorage::new_local_store(&Path) -> Result<_, std::io::Error>` | `ObjectStorage::new_local_filesystem(&Path) -> Result<_, StorageError>`, or the free `new_local_filesystem_storage(&Path)` |
| `S3Storage::new_s3_store(bucket, prefix, Option<&S3Config>).await` | the free, **synchronous** `new_s3_storage(S3Options, String, Option<String>, Option<S3Credentials>)` |
| `storage::s3::S3Config { region, endpoint, credentials, allow_http }` | `config::S3Options { region, endpoint_url, anonymous, allow_http }` — **`endpoint` renamed to `endpoint_url`, `credentials` moved out, `anonymous` added** |
| `storage::s3::StaticS3Credentials { .. }` | `config::S3StaticCredentials { .., expires_after: Option<DateTime<Utc>> }` — **renamed, and it gained an expiry** |
| `storage::s3::S3Credentials` | `config::S3Credentials` — moved, and it gained `Refreshable(Arc<dyn CredentialsFetcher>)` |
| `MemCachingStorage::new(storage, a, b, c, d)` | `MemCachingStorage::new(storage, a, b, c, d, e)` — **six arguments** |
| `trait Storage: Debug + Sealed` | `trait Storage: Debug + Sealed + Sync + Send` |

`Storage` also gained the operations this milestone needs:
`fetch_config`/`update_config` (with ETag semantics),
`fetch_transaction_log`/`write_transaction_log`, and the listing and deleting
operations the garbage collector uses.

### 5.7 Virtual references

| Milestone 01 | This milestone |
| --- | --- |
| `storage::virtual_ref::VirtualChunkResolver` (a **trait**) | `virtual_chunks::VirtualChunkResolver` (a **concrete struct**) |
| `ObjectStoreVirtualChunkResolver::new(Option<ObjectStoreVirtualChunkResolverConfig>)` | **removed** → `virtual_chunks::{S3Fetcher, LocalFSFetcher}` behind `trait ChunkFetcher` |
| `ObjectStoreVirtualChunkResolverConfig` | **removed** → `VirtualChunkContainer { name, url_prefix, store }` plus per-container `Credentials` |
| `storage::virtual_ref::construct_valid_byte_range` | `session::construct_valid_byte_range` |
| `VirtualChunkLocation::Absolute(String)` (an enum variant) | `VirtualChunkLocation(pub String)` — **a tuple struct** |
| `VirtualChunkRef { location, offset, length }` | plus `checksum: Option<Checksum>` — **a new required field** |
| `format::manifest::VirtualReferenceError` | **unchanged path.** The module around it moved to `virtual_chunks`, the error type did not: it is `icechunk::format::manifest::VirtualReferenceError` at both milestones, and that is the path the drivers spell. Section 4.6 and section 8 name it unqualified; section 4.8 declares it |

### 5.8 `format`, `change_set` and `refs`

| Milestone 01 | This milestone |
| --- | --- |
| `pub type NodeId = u32;` | `pub type NodeId = ObjectId<8, NodeTag>;` — **no longer `Copy`; clone it; `NodeId::random()`** |
| `pub struct ByteRange(pub Bound<ChunkOffset>, pub Bound<ChunkOffset>)` | `pub enum ByteRange { Bounded(Range<_>), From(_), Last(_) }` |
| `ByteRange::ALL == Self(Unbounded, Unbounded)` | `ByteRange::ALL == Self::From(0)` |
| `ByteRange::length()` | **removed** |
| — | new: `impl From<Range<ChunkOffset>> for ByteRange` |
| `type IcechunkFormatVersion` (private) | `pub type IcechunkFormatVersion = u16;` |
| `Manifest::size()` | `Manifest::len()` + `Manifest::is_empty()` |
| `Manifest::get_chunk_payload(node: NodeId, ..)` | `get_chunk_payload(node: &NodeId, ..)` |
| `Manifest::iter(self: Arc<Self>, node: &NodeId)` | `Manifest::iter(self: Arc<Self>, node: NodeId, ndim: usize)` |
| `Snapshot::size()` | `Snapshot::len()` + `Snapshot::is_empty()` |
| — | new: `ZarrArrayMetadata::valid_chunk_coord`, `Manifest::from_stream`, `format::transaction_log` |
| `ChangeSet::{delete_group, delete_array}(path, node_id: NodeId)` | `(path, node_id: &NodeId)` |
| `ChangeSet::is_deleted(&self, path)` | `is_deleted(&self, path, node_id: &NodeId)` |
| `ChangeSet::new_nodes() -> impl Iterator<Item = &Path>` | `-> impl Iterator<Item = (&Path, &NodeId)>` |
| `ChangeSet::{export_to_bytes, import_from_bytes} -> RepositoryResult<_>` | `-> SessionResult<_>` |
| `refs::list_refs(..) -> RefResult<Vec<Ref>>` | `-> RefResult<HashSet<Ref>>` |
| — | new on `refs`: `list_tags`, `list_branches`, `delete_branch`, `fetch_ref`, `Ref::fetch` |

`SnapshotMetadata { id, written_at, message }` is structurally unchanged; only
its import path moved.

### 5.9 Import lines that must be rewritten

```rust
// milestone 01                                  ->  this milestone
use icechunk::zarr::{AccessMode, Store};         //  use icechunk::{Store, store::StoreError};
use icechunk::{RepositoryBuilder, SnapshotMetadata};
                                                 //  use icechunk::format::snapshot::SnapshotMetadata;
use icechunk::repository::{get_chunk, ChangeSet, ChunkPayload, ZarrArrayMetadata,
                           ChunkKeyEncoding, ChunkShape, Codec, DataType,
                           FillValue, StorageTransformer};
//  -> use icechunk::session::get_chunk;
//     use icechunk::change_set::ChangeSet;
//     use icechunk::format::manifest::ChunkPayload;
//     use icechunk::format::snapshot::ZarrArrayMetadata;
//     use icechunk::metadata::{ChunkKeyEncoding, ChunkShape, Codec, DataType,
//                              FillValue, StorageTransformer, UserAttributes};
use icechunk::storage::s3::{S3Config, S3Credentials, S3Storage, StaticS3Credentials};
//  -> use icechunk::config::{S3Credentials, S3Options, S3StaticCredentials};
//     use icechunk::storage::new_s3_storage;
use icechunk::storage::virtual_ref::{ObjectStoreVirtualChunkResolverConfig,
                                     VirtualChunkResolver, construct_valid_byte_range};
//  -> use icechunk::virtual_chunks::VirtualChunkContainer;
//     use icechunk::session::construct_valid_byte_range;
```

## 6. Formats and values crossing the boundary

- **Zarr metadata documents** are JSON `Bytes` written at `zarr.json` keys. The
  drivers write documents by hand and read your value back. Field **order** in
  what you emit is never asserted; what is asserted is that a document you
  accepted comes back with the same field names and values, that an array
  document creates an array and a group document a group, and that a byte range
  applied to a metadata read slices what a whole read returns. The comparison is
  made on the **parsed JSON**, so neither member order nor whitespace is
  asserted: you may re-emit a document you accepted in any layout.

  The subset that must parse is the Zarr v3 subset milestone 01 already
  required, unchanged here — these member names, and no others:

  - a **group** document: `zarr_format`, `node_type` (which must be the word
    `group`), and an optional free-form `attributes` object;
  - an **array** document: `zarr_format`, `node_type` (the word `array`),
    optional `attributes`, `shape`, `data_type`, `chunk_grid` (a
    `{name, configuration}` pair whose name is `regular` and whose configuration
    carries `chunk_shape`), `chunk_key_encoding` (a `{name, configuration}` pair
    whose name is `default` and whose configuration carries `separator`, which
    is `/`), `fill_value`, `codecs`, optional `storage_transformers`, and
    `dimension_names`.

  `node_type` is what tells the two apart, and the array document's members map
  onto section 4.8's `ZarrArrayMetadata` field for field — note that the JSON
  member is `chunk_grid` where the Rust field is `chunk_shape`. `data_type`'s
  value is the **lower-case Zarr name**, unchanged from milestone 01 — `bool`,
  `int8` … `uint64`, `float16`, `float32`, `float64`, `complex64`,
  `complex128`, `string`, `bytes` — and that is the one textual form
  `DataType`'s `TryFrom<&str>`, its `Display` and its serde all use. Anything beyond
  this subset is out of scope (PROFILE §P2): codecs and storage transformers are
  carried as opaque `{name, configuration}` pairs and never executed, chunk grids
  other than `regular` and separators other than `/` are not required, and no
  chunk is ever decoded.
- **Chunk bytes** are opaque `Bytes`. A byte range applied to a chunk read
  slices what a whole read returns, for all three of `Bounded`, `From` and
  `Last`.
- **Virtual chunk locations** are URL strings.
  `VirtualChunkLocation::from_absolute_path` parses and **normalises**: it keeps
  the scheme and the host, strips empty path segments, and re-forms
  `scheme://host/path`, so `"s3://bucket//a//b"` becomes `"s3://bucket/a/b"` and
  a `file://` URL keeps its empty host as `file:///abs/path`. A string that is
  not a URL, and an `s3` URL with no host, are both refused. The normalised
  string is what `all_virtual_chunk_locations` yields.
- **Snapshot ids, node ids and chunk ids** are opaque handles. The drivers
  compare them for equality and carry them between calls; they never assert a
  length, an encoding or a textual form.
- **Configuration** is one object per repository plus an `ETag` string. The
  drivers never parse the stored bytes; they round-trip through
  `fetch_config` / `save_config`. An `ETag` is a non-empty string.
- **Errors** are reported through the crate's `Result` types, and only as
  categories — see section 8.

## 7. Determinism and defaults

- Nothing the drivers run touches the network, a cloud account, MinIO, or an
  ambient AWS environment. Do not read credentials from the environment on a
  path that in-memory or local-filesystem storage reaches.
- Every asynchronous entry point must complete on both a single-threaded and a
  multi-threaded tokio runtime, without the caller installing anything.
- No in-scope behaviour may depend on wall-clock time, on a sleep, or on the order
  two unsynchronised tasks happen to run in.
- Documented defaults, all of them asserted: `inline_chunk_threshold_bytes` is
  512; `unsafe_overwrite_refs` is `false`; `get_partial_values_concurrency` is
  10; the default virtual chunk containers are the five of PROFILE §D5; the
  default branch is `main`; a new repository has exactly one branch and no tags;
  `BasicConflictSolver::default()` is `UseOurs` for both conflict knobs and
  `false` for both delete flags; `ByteRange::ALL` is `From(0)`.
- Where a document fixes no order, none is asserted (PROFILE P4). Where one is
  fixed — ancestry newest first, `get_partial_values` in request order — it is.

## 8. Error categories

Where a case is *about* a failure, the drivers match the **variant** below;
where a case merely needs an operation to fail, they assert only that it does.
Either way they never look at an error's text. Each row is a category that must
exist and must be what the named situation produces.

| Category | Produced by |
| --- | --- |
| `RepositoryError::AlreadyInitialized` | creating a repository where one exists |
| `RepositoryError::RepositoryDoesntExist` | opening where none exists |
| `RepositoryError::SnapshotNotFound { id }` | naming a snapshot that is not stored — `reset_branch`, `readonly_session(SnapshotId)` |
| `RepositoryError::StorageError(StorageError)` | a storage-level failure surfacing through the repository |
| `RepositoryError::StorageError(StorageError::Other(String))` | credentials that do not match a container's store type, at `create`/`open` |
| `StorageError::ConfigUpdateConflict` | `save_config` from an instance whose configuration view was superseded |
| `SessionError::ReadOnlySession` | `merge`, `commit` or `rebase` on a read-only session |
| `SessionError::NoChangesToCommit` | committing an empty change set |
| `SessionError::Conflict { expected_parent, actual_parent }` | committing when the branch tip moved; both fields are `Option<SnapshotId>` and both are `Some` |
| `SessionError::RebaseFailed { snapshot, conflicts }` | a rebase that could not resolve; `snapshot: SnapshotId` names where it stopped, `conflicts: Vec<Conflict>` says why |
| `SessionError::InvalidIndex { coords, path }` | a chunk coordinate outside the array |
| `SessionError::NodeNotFound { .. }` | reading a node that is not there |
| `SessionError::NotAnArray { .. }` / `NotAGroup { .. }` | `get_array` on a group, `get_group` on an array |
| `SessionError::AlreadyExists { .. }` | adding a node where one exists |
| `SessionError::VirtualReferenceError(VirtualReferenceError)` | a virtual chunk read that failed |
| `VirtualReferenceError::NoContainerForUrl(String)` | no container's prefix matches the location |
| `VirtualReferenceError::ObjectModified(String)` | the referenced object no longer satisfies the recorded checksum |
| `VirtualReferenceError::CannotParseUrl(_)` | `from_absolute_path` on something that is not a URL |
| `VirtualReferenceError::CannotParseBucketName(_)` | `from_absolute_path` on a URL with no host, other than `file` |
| `StoreError::ReadOnly` | a write through a read-only store |
| `StoreError::NotFound(KeyNotFoundError)` | reading a key that holds nothing |
| `StoreError::NotAllowed(String)` | `set_virtual_ref` at a non-chunk key |
| `StoreError::Unimplemented(&'static str)` | `set_partial_values`, and Zarr-v2 metadata writes |
| `StoreError::SessionError(SessionError)` | a session-level failure surfacing through the store |
| `StoreError::InvalidKey { key }` | a key that does not parse |

`RepositoryError`, `SessionError`, `StoreError`, `KeyNotFoundError` and
`VirtualReferenceError` are `#[non_exhaustive]`, and the drivers always match
with a wildcard arm, so adding a variant to any of them is safe. Removing or
renaming a variant in this table is not. `StorageError` is *not*
`#[non_exhaustive]`; the only variant of it this table names is
`ConfigUpdateConflict`, plus `Other(String)` reached through
`RepositoryError::StorageError`.

## 9. Out of scope at this milestone

Out of scope at every milestone:

- Internal module layout below the paths section 4 names, private type and
  function names, file organisation under `icechunk/src/`.
- Object-store key names, prefixes and on-disk encodings (PROFILE P3).
- Error message wording, error-variant *identity* beyond the categories section
  8 names, and the **text** `Debug` and `Display` produce. Whether those impls
  exist is required by sections 3 and 8; only what they print is out of scope.
- Iteration order no document fixes, allocation strategy, timing, memory use,
  throughput, and how much anything caches.
- Your own tests, benchmarks, examples, documentation and CI configuration.
- Anything reachable only from a `#[cfg(test)]` module. The drivers link your
  crate as an ordinary dependency, so a test-only module is invisible to them
  and nothing in one is ever in scope — put nothing section 4 names behind
  `#[cfg(test)]`.

Additionally, out of scope at **this** milestone:

- **The Zarr array format itself** (PROFILE P2): codecs, data-type semantics,
  fill-value encoding, compression, sharding.
- **Real cloud storage and real credentials** (PROFILE P1): S3, GCS, Azure and
  Tigris as *repository* storage, and S3, GCS, Azure and Tigris as *virtual
  chunk* backends. Their configuration types must exist and must be
  constructible; no request is ever made. Credential **refresh** on expiry —
  `S3Credentials::Refreshable` and `S3StaticCredentials::expires_after` — cannot
  be observed without a live endpoint and is out of scope. The types must still
  exist, because section 4.5 names them in signatures a caller writes; what is
  in scope is that `Credentials` values can be supplied per container.
- **The Python binding.** Nothing in `icechunk-python/` is built, imported or
  required. Where a design document describes a Python spelling, what is required
  is the Rust behaviour it implies.
- **Wall-clock performance**, concurrency levels actually achieved, and whether
  `get_partial_values` runs its fetches in parallel. Its *concurrency limit* is
  a configuration value that must be accepted and its *result order* is in scope;
  the parallelism is not.
- **Which unreachable objects garbage collection deletes.** Section 4.9's three
  properties are in scope; a sweep's exact deletion counts for unreachable data,
  the treatment of dangling attribute files and dangling transaction logs, and
  reference expiry are a later milestone's capability.
- **Write-time validation of a virtual reference's container** (PROFILE §D4):
  storing a reference whose URL no container authorises must **not** fail here.
- **Per-field configuration merge** (PROFILE §D3): supplying a partial
  configuration and having the persisted values fill the rest is a later
  milestone.
- **Which half of `open`'s precondition failed.** Section 4.2 requires `open` to
  refuse with `RepositoryDoesntExist` both when `main` is missing and when no
  configuration object is stored. Only the first half is in scope: no sequence of
  calls in this contract produces a repository that has `main` but no
  configuration object, and reaching that state would mean deleting an
  object-store key by hand, which PROFILE §P3 puts out of scope at every
  milestone. The obligation stands; it is simply not observable from here.
- **Whether a read-only session's per-node recorders refuse** (PROFILE §D6), and
  `Store::clear` on a read-only store. What is in scope is that a read-only
  session cannot publish and that the store's key-level writes refuse.
- **The `VirtualChunkResolver` type's own methods.** It is named in section 4.6
  because `Repository` builds one, but the drivers reach virtual chunks only
  through a session or a store; `new`, `get_fetcher` and `fetch_chunk` on it are
  not exercised.
- **`TransactionLog`'s contents.** The type must exist with the fields section
  4.8 names because `ConflictSolver::solve` receives one, but the drivers never
  build one and never inspect one; what a commit records in it is observed only
  through the conflicts a rebase reports.
- **`ChangeSet`'s own methods.** The drivers construct one only with `Default`,
  and otherwise obtain one from `Session::discard_changes` or `Session::into()`
  and hand it straight to `Session::merge`. They never inspect it, so
  `is_empty`, `merge` and the byte round trip on `ChangeSet` itself are listed
  in section 4.8 for completeness and are not exercised.
- **`Snapshot` and `Manifest` as constructible types.** They appear in section
  4.8 for completeness; the drivers reach their contents only through
  `Session` and `Repository`.
- **The mere *existence* of a type is never itself a requirement group.** Several
  items above are required by the contract but exercised by nothing:
  `MemCachingStorage`, `ConflictResolution`, `TransactionLog` and its fields,
  `CredentialsFetcher`, `ChunkFetcher` / `S3Fetcher` / `LocalFSFetcher`, and
  `VirtualChunkResolver`. Removing one of them still breaks the contract — each
  is declared by section 4, and `ConflictResolution`, `TransactionLog`,
  `CredentialsFetcher` and `ChunkFetcher` are named in signatures or variants
  section 4 states — but the only way to check it directly would be a case that
  names the type and asserts nothing, and this suite deliberately drives every
  in-scope requirement through behaviour instead.
  `ConflictResolution` is the sharpest case: observing it means implementing
  `ConflictSolver`, whose `#[async_trait]` signature would make a proc-macro
  crate part of the verifier's own dependency set, which section 3 fixes on
  purpose. They are therefore **out of scope at this milestone**, by decision
  rather than by oversight.
- **Commit properties.** `commit` takes `Option<SnapshotProperties>`; the
  drivers always pass `None`. What a non-`None` value does is a later
  milestone's capability.
- **Ancestry beyond a single snapshot's local history.** A repository with more
  commits than one snapshot's history window keeps is out of scope here.
