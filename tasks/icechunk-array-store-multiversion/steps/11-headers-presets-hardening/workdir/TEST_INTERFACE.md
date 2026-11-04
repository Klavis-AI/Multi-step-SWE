# TEST_INTERFACE.md — milestone 11, "headers, presets and hardening"

The public contract the hidden verifier is written against. It is
**self-contained**: everything a requirement group touches is named here, including
the parts this milestone inherits from the ten before it. Where this document
and `icechunk-spec/spec.md` appear to disagree, `icechunk-spec/PROFILE.md`
resolves it, and the resolution there wins.

Nothing below is a hint about how to implement anything. It is the boundary: a
name, a signature, an argument order, an error category, an exit status, an
output shape. Hidden tests may combine these in ways this document does not
show, but they never reach for an interface this document does not name.

---

## 1. The artifact

A Cargo workspace at `/app`. Nine members; the default member is the library
crate `icechunk` at `/app/icechunk`.

```toml
members = [
  "icechunk", "icechunk-types", "icechunk-format", "icechunk-storage",
  "icechunk-s3", "icechunk-arrow-object-store", "icechunk-python",
  "icechunk-macros", "icechunk-js",
]
default-members = ["icechunk"]
```

| fact | value |
| --- | --- |
| crate under test | `icechunk` |
| version | `2.2.0` — every workspace crate moves in lockstep |
| edition | `2024`; workspace `rust-version` `1.91.1` |
| `Cargo.lock` | committed at the workspace root |

**Default features of `icechunk`** (all on unless the candidate changes them):

```toml
default = ["s3", "object-store-s3", "object-store-gcs", "object-store-azure",
           "object-store-http", "object-store-fs", "redirect"]
cli     = ["dep:clap", "dep:anyhow", "dep:dialoguer", "dep:dirs"]   # NOT default
```

**How the verifier builds the artifact — both commands must succeed:**

```sh
cargo build --manifest-path /app/Cargo.toml
cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk
```

The second produces the `icechunk` binary described in §4. Its path is handed
to the drivers in the environment variable `ICECHUNK_TEST_CLI_BIN`.

**How the verifier reaches the library.** A throw-away driver crate outside
`/app` declares `icechunk = { path = "/app/icechunk" }` and is compiled as an
integration test. It therefore sees **only `pub` items reachable from
`icechunk::`** — never a `pub(crate)` item, never a `#[cfg(test)]` module, never
a sibling crate that `icechunk` does not re-export. Anything this document names
must be public at exactly the path it gives. The driver's own dependencies are
`bytes`, `chrono`, `futures`, `serde_json`, `tempfile` and `tokio`; the
candidate's manifests are never modified.

The module layout the paths below assume is the one this release already has:

```rust
pub use icechunk_format as format;   // icechunk::format::* lives in icechunk-format
pub mod asset_manager;  pub mod change_set;  pub mod cli;      pub mod compat;
pub mod config;         pub mod conflicts;   pub mod diff;     pub mod display;
pub mod error;          pub mod feature_flags;                 pub mod inspect;
pub mod migrations;     pub mod ops;         pub mod refs;     pub mod repository;
pub mod session;        pub mod storage;     pub mod store;    pub mod virtual_chunks;
pub use config::{ObjectStoreConfig, RepositoryConfig};
pub use repository::Repository;
pub use storage::{ObjectStorage, Storage, StorageError, new_in_memory_storage};
pub use store::Store;
```

---

## 2. The inherited contract, as this release spells it

Ten milestones precede this one. A candidate carries one workspace forward, so
each of them is still required, and each is still checked here — through the
names and signatures **this** release uses, which are not always the ones the
milestone introduced. Only what a requirement group touches is listed.

### 2.1 Milestone 01 — a transactional, versioned repository

```rust
// icechunk
pub async fn new_in_memory_storage() -> StorageResult<Arc<dyn Storage + Send + Sync>>;
pub async fn new_local_filesystem_storage(path: &std::path::Path)
    -> StorageResult<Arc<dyn Storage + Send + Sync>>;      // feature object-store-fs

// icechunk::Repository
pub async fn create(config: Option<RepositoryConfig>,
                    storage: Arc<dyn Storage + Send + Sync>,
                    authorize_virtual_chunk_access: HashMap<String, Option<Credentials>>,
                    spec_version: Option<SpecVersionBin>,
                    check_clean_root: bool) -> RepositoryResult<Repository>;
pub async fn open(config: Option<RepositoryConfig>,
                  storage: Arc<dyn Storage + Send + Sync>,
                  authorize_virtual_chunk_access: HashMap<String, Option<Credentials>>)
    -> RepositoryResult<Repository>;

pub async fn writable_session(&self, branch: &str) -> RepositoryResult<Session>;
pub async fn readonly_session(&self, version: &VersionInfo) -> RepositoryResult<Session>;
pub async fn list_branches(&self) -> RepositoryResult<BTreeSet<String>>;
pub async fn lookup_branch(&self, branch: &str) -> RepositoryResult<SnapshotId>;
pub async fn create_branch(&self, name: &str, at: &SnapshotId) -> RepositoryResult<()>;
pub async fn delete_branch(&self, branch: &str) -> RepositoryResult<()>;
pub async fn list_tags(&self) -> RepositoryResult<BTreeSet<String>>;
pub async fn lookup_tag(&self, tag: &str) -> RepositoryResult<SnapshotId>;
pub async fn create_tag(&self, tag: &str, at: &SnapshotId) -> RepositoryResult<()>;
pub async fn delete_tag(&self, tag: &str) -> RepositoryResult<()>;
pub async fn lookup_snapshot(&self, id: &SnapshotId) -> RepositoryResult<SnapshotInfo>;
pub async fn ancestry(&self, version: &VersionInfo)
    -> RepositoryResult<impl Stream<Item = RepositoryResult<SnapshotInfo>> + Send>;
pub fn asset_manager(&self) -> &Arc<AssetManager>;
pub fn spec_version(&self) -> SpecVersionBin;

// icechunk::repository::VersionInfo — #[non_exhaustive]
pub enum VersionInfo { SnapshotId(SnapshotId), TagRef(String),
                       BranchTipRef(String), AsOf { branch: String, at: DateTime<Utc> } }

// icechunk::session::Session
pub async fn add_group(&mut self, path: Path, definition: Bytes) -> SessionResult<()>;
pub async fn add_array(&mut self, path: Path, shape: ArrayShape,
                       dimension_names: Option<Vec<DimensionName>>,
                       user_data: Bytes) -> SessionResult<()>;
pub async fn set_chunk_ref(&mut self, path: Path, coord: ChunkIndices,
                           data: Option<ChunkPayload>) -> SessionResult<()>;
pub async fn delete_chunks(&mut self, node_path: &Path,
                           coords: impl IntoIterator<Item = ChunkIndices>) -> SessionResult<()>;
pub async fn get_node(&self, path: &Path) -> SessionResult<NodeSnapshot>;
pub async fn get_array(&self, path: &Path) -> SessionResult<NodeSnapshot>;
pub async fn get_chunk_ref(&self, path: &Path, coords: &ChunkIndices)
    -> SessionResult<Option<ChunkPayload>>;
pub async fn list_nodes<'a>(&'a self, parent_group: &Path)
    -> SessionResult<impl Iterator<Item = SessionResult<NodeSnapshot>> + use<'a>>;
pub fn read_only(&self) -> bool;
pub fn branch(&self) -> Option<&str>;
pub fn commit(&mut self, message: impl Into<String>) -> CommitBuilder<'_>;
```

Reference rules (`PROFILE.md` P6 states them in full, and P11 fixes the
granularity: the **category**, never the message):

- a fresh repository has exactly one branch, `main`, and no tags;
- `create_branch` / `create_tag` at a snapshot that was never written fail;
- a tag name is unique, and a deleted tag's name is retired permanently;
- `delete_branch("main")` is refused with
  `RepositoryError { kind: RepositoryErrorKind::CannotDeleteMain, .. }` — both
  at `icechunk::repository::` — and `main` still resolves afterwards.
  `RepositoryErrorKind` is `#[non_exhaustive]`, so a match over it carries a
  wildcard arm.

Identity and paths:

- `SnapshotId`, `ManifestId`, `ChunkId` are `ObjectId<12, _>`; `NodeId` is
  `ObjectId<8, _>`. `Display` renders a 12-byte id as **20 Crockford base-32
  characters** (upper-case letters and digits), and `TryFrom<&str>` parses that
  back. `TryFrom<&[u8]>` fails for any slice that is not the right width.
  `random()` is public on **every** `ObjectId` width and tag, so
  `SnapshotId::random()`, `ManifestId::random()`, `ChunkId::random()` and
  `NodeId::random()` all exist.
- `ChunkIndices` is a public tuple struct over a public `Vec<u32>` —
  `ChunkIndices(pub Vec<u32>)` — so a caller builds one as
  `ChunkIndices(vec![0, 1])` and reads its coordinates back out of `.0`.
  `DimensionName` implements `From<&str>` and `From<Option<&str>>`.
- `RepositoryConfig`'s fields are **public and directly assignable**, as well as
  readable through the accessors §2.6 lists: `inline_chunk_threshold_bytes`,
  `get_partial_values_concurrency`, `compression`, `max_concurrent_requests`,
  `caching`, `storage`, `virtual_chunk_containers`, `manifest`. Every one of
  them is an `Option`, and `RepositoryConfig::default()` leaves them `None`.
- `icechunk::format::Path`: `Path::root()`, `Path::new(&str) -> Result<Path,
  PathError>` and `TryFrom<&str>/<String>`. A relative path is `PathError::NotAbsolute`
  and a non-canonical one `PathError::NotCanonic`.
- `ArrayShape::new(impl IntoIterator<Item = (u64 /*array_length*/, u32 /*num_chunks*/)>)
  -> Option<ArrayShape>`, with `DimensionShape::{array_length, num_chunks}` accessors.

`icechunk::Store` — the Zarr-facing key/value view of a session:

```rust
pub async fn from_session(session: Arc<tokio::sync::RwLock<Session>>) -> Store;
pub fn session(&self) -> Arc<tokio::sync::RwLock<Session>>;
pub async fn get(&self, key: &str, byte_range: &ByteRange) -> StoreResult<Bytes>;
pub async fn set(&self, key: &str, value: Bytes) -> StoreResult<()>;
pub async fn exists(&self, key: &str) -> StoreResult<bool>;
pub async fn delete(&self, key: &str) -> StoreResult<()>;
pub async fn delete_dir(&self, prefix: &str) -> StoreResult<()>;
pub async fn list(&self)               -> StoreResult<impl Stream<Item = StoreResult<String>> + Send>;
pub async fn list_prefix(&self, p: &str) -> StoreResult<impl Stream<Item = StoreResult<String>> + Send>;
pub async fn list_dir(&self, p: &str)    -> StoreResult<impl Stream<Item = StoreResult<String>> + Send>;
```

Key grammar: `zarr.json` and `<path>/zarr.json` are metadata keys;
`<array>/c/<i>/<j>/…` is a chunk key. `ByteRange::ALL` and
`ByteRange::bounded(start, end)` are public. Group and array metadata documents
are Zarr v3 JSON; the subset that must parse is the one `PROFILE.md` fixes.

### 2.2 Milestone 02 — sessions, conflict detection and rebase

- Two writable sessions on the same branch may both mutate; the second to
  commit fails with `SessionErrorKind::Conflict { expected_parent, actual_parent }`.
- `Session::rebase(&mut self, solver: &(dyn ConflictSolver + Send + Sync))
  -> SessionResult<()>`, after which the commit succeeds.
- `icechunk::conflicts::basic_solver::{BasicConflictSolver, VersionSelection}`;
  `BasicConflictSolver { on_chunk_conflict: VersionSelection::{Fail, UseOurs, UseTheirs},
  fail_on_delete_of_updated_array: bool, fail_on_delete_of_updated_group: bool }`,
  and `Default` is `UseOurs, false, false`.
  `icechunk::conflicts::{Conflict, ConflictResolution, ConflictSolver}` and
  `icechunk::conflicts::detector::ConflictDetector` are public too.
  `ConflictDetector` only **reports**: it is the solver that patches nothing, so
  `rebase` through it succeeds when the concurrent changes do not actually
  conflict, and **fails** when they do. Resolving a real conflict needs a solver
  that resolves, such as `BasicConflictSolver`.
- A session that has committed becomes **read-only**: `read_only()` is true,
  `branch()` is `None`, and any further edit is an error.
- Virtual chunk references: `ChunkPayload::Virtual(VirtualChunkRef { location,
  offset, length, checksum })` with
  `VirtualChunkLocation::from_url(&str) -> Result<_, VirtualReferenceError>` and
  `.url()`. A reference is only readable through an **authorized container**;
  see §2.6 for the prefix rule and §2.8 for the credential sentinels.
  Reading goes through

  ```rust
  // icechunk::session::Session
  pub async fn get_chunk_reader(&self, path: &Path, coords: &ChunkIndices,
                                byte_range: &ByteRange)
      -> SessionResult<Option<IcechunkBoxFuture<'_, SessionResult<Bytes>>>>;
  // icechunk::session — free function
  pub async fn get_chunk(reader: Option<IcechunkBoxFuture<'_, SessionResult<Bytes>>>)
      -> SessionResult<Option<Bytes>>;
  ```

  (`IcechunkBoxFuture` lives in `icechunk::compat` and a driver never names it:
  the reader goes straight from one call into the other.) A location outside
  every authorized prefix must **fail** rather than be read.

### 2.3 Milestone 03 — expiration, garbage collection, file headers

```rust
// icechunk::ops::gc
pub enum ExpiredRefAction { Delete, Ignore }
pub struct ExpireResult { pub released_snapshots: HashSet<SnapshotId>,
                          pub edited_snapshots: HashSet<SnapshotId>,
                          pub deleted_refs: HashSet<Ref> }
pub async fn expire(asset_manager: Arc<AssetManager>, older_than: DateTime<Utc>,
                    expired_branches: ExpiredRefAction, expired_tags: ExpiredRefAction,
                    repo_update_retries: Option<&RepoUpdateRetryConfig>,
                    num_updates_per_repo_info_file: u16) -> GCResult<ExpireResult>;

pub struct GCConfig { /* private fields */ }
impl GCConfig {
    pub fn clean_all(chunks_age: DateTime<Utc>, metadata_age: DateTime<Utc>,
                     extra_roots: Option<HashSet<SnapshotId>>,
                     max_snapshots_in_memory: NonZeroU16,
                     max_compressed_manifest_mem_bytes: NonZeroUsize,
                     max_concurrent_manifest_fetches: NonZeroU16,
                     dry_run: bool) -> Self;
    pub fn action_needed(&self) -> bool;
    pub fn deletes_snapshots(&self) -> bool;
}
pub struct GCSummary { pub bytes_deleted: u64, pub chunks_deleted: u64,
                       pub manifests_deleted: u64, pub snapshots_deleted: u64,
                       pub attributes_deleted: u64, pub transaction_logs_deleted: u64 }
pub async fn garbage_collect(asset_manager: Arc<AssetManager>, config: &GCConfig,
                             repo_update_retries: Option<&RepoUpdateRetryConfig>,
                             num_updates_per_repo_info_file: u16) -> GCResult<GCSummary>;
```

`expire` squashes history: every snapshot older than the cutoff that no
reference protects is *released*, and the oldest surviving snapshot of each
ancestry is *edited* to re-parent onto the repository root. `garbage_collect`
then removes the released files. A `dry_run` configuration reports what it
would do and deletes nothing.

**The metadata file header** — every metadata object begins with a fixed
39-byte header, and it is readable without decoding the body:

```rust
// icechunk::format::format_constants
pub const ICECHUNK_FORMAT_MAGIC_BYTES: &[u8];    // "ICE🧊CHUNK", 12 bytes
pub const ICECHUNK_IMPL_NAME_LEN: usize = 24;    // space-padded implementation name
pub const ICECHUNK_SPEC_VERSION_OFFSET: usize = 36;
pub const ICECHUNK_FILE_TYPE_OFFSET: usize = 37;
pub const ICECHUNK_COMPRESSION_OFFSET: usize = 38;
pub const ICECHUNK_FILE_HEADER_LEN: usize = 39;

pub enum SpecVersionBin { V1 = 1, V2 = 2 }               // Default + current() == V2
pub enum FileTypeBin { Snapshot = 1, Manifest = 2, Attributes = 3,
                       TransactionLog = 4, Chunk = 5, RepoInfo = 6 }
pub enum CompressionAlgorithmBin { None = 0, Zstd = 1 }
pub struct FileHeader { pub impl_name: String, pub app_name: String,
                        pub app_version: Option<String>,
                        pub spec_version: SpecVersionBin, pub file_type: FileTypeBin,
                        pub compression: CompressionAlgorithmBin }
pub fn parse_file_header(buf: &[u8]) -> Result<FileHeader, IcechunkFormatError>;

pub const ICECHUNK_LIB_VERSION: &str;               // the crate version
pub static ICECHUNK_CLIENT_NAME: LazyLock<String>;  // "ic-" + ICECHUNK_LIB_VERSION
```

`parse_file_header` **discovers** the file type rather than asserting one. It
must reject a buffer shorter than `ICECHUNK_FILE_HEADER_LEN` and one whose magic
bytes are wrong. `impl_name` is the trimmed 24-byte field; `app_name` is it up
to the first `-` and `app_version` the rest (`None` when there is no `-`).

Snapshot, manifest, transaction-log and repo-info files this library writes are
**Zstd-compressed**, so their header's compression byte is
`CompressionAlgorithmBin::Zstd` (`"zstd"`). Chunk objects carry no header.

`AssetManager` exposes the header of each kind of file without fetching the
body:

```rust
// icechunk::asset_manager::AssetManager
pub async fn fetch_header(&self, path: &str)                  -> RepositoryResult<FileHeader>;
pub async fn fetch_repo_info_header(&self)                    -> RepositoryResult<FileHeader>;
pub async fn fetch_snapshot_header(&self, id: &SnapshotId)    -> RepositoryResult<FileHeader>;
pub async fn fetch_manifest_header(&self, id: &ManifestId)    -> RepositoryResult<FileHeader>;
pub async fn fetch_transaction_log_header(&self, id: &SnapshotId) -> RepositoryResult<FileHeader>;
// and the body-fetching counterparts
pub async fn fetch_snapshot(&self, id: &SnapshotId)     -> RepositoryResult<Arc<Snapshot>>;
pub async fn fetch_transaction_log(&self, id: &SnapshotId) -> RepositoryResult<Arc<TransactionLog>>;
pub async fn fetch_repo_info(&self) -> RepositoryResult<(Arc<RepoInfo>, storage::VersionInfo)>;
pub async fn list_snapshots(&self)
    -> RepositoryResult<BoxStream<'_, RepositoryResult<ListInfo<SnapshotId>>>>;
```

```rust
// icechunk::storage
pub struct ListInfo<A> { pub id: A, pub created_at: DateTime<Utc>, pub size_bytes: u64 }
```

`created_at` is the **object store's** timestamp for that object, which §3.6
depends on. `SnapshotInfo::flushed_at` (§2.5) is the writing **host's**
timestamp for the same snapshot, and the two are not the same clock.

One `Storage` decorator is part of the in-scope surface, because §3.6 is observed
through it:

```rust
// icechunk::storage::latency
pub struct LatencyStorage { /* private fields */ }
impl LatencyStorage {
    pub fn new(backend: Arc<dyn Storage + Send + Sync>,
               write_delay_ms: u64, read_delay_ms: u64) -> Self;
}
```

`LatencyStorage` implements `Storage`, so it is usable anywhere an
`Arc<dyn Storage + Send + Sync>` is. It adds no I/O of its own and applies each
delay **before** it delegates — a write sleeps `write_delay_ms` and *then*
calls the inner store — so the object the inner store finally stamps carries a
`created_at` at least that much later than any timestamp the caller took before
the call. `PROFILE.md` P13 and P22 say why that ordering is load-bearing.

### 2.4 Milestones 04 and 06 — the FlatBuffers format and manifest splitting

```rust
// icechunk::format::snapshot::Snapshot
pub fn from_buffer(spec_version: SpecVersionBin, buffer: Vec<u8>) -> IcechunkResult<Snapshot>;
pub fn bytes(&self) -> &[u8];
pub fn id(&self) -> SnapshotId;
pub fn message(&self) -> String;
pub fn get_node(&self, path: &Path) -> IcechunkResult<Arc<NodeSnapshot>>;
```

`from_buffer` **verifies** the buffer once, up front: a truncated or corrupt
buffer is an error rather than an access-time panic. A snapshot written by this
library must round-trip through `bytes()` → `from_buffer(..)`.

A node's Zarr metadata is opaque: `NodeSnapshot { id, path, user_data: Bytes,
node_data }`, and `user_data` round-trips byte for byte, unknown Zarr members
included. `NodeData::Array { shape, dimension_names, manifests }` /
`NodeData::Group`; `NodeType::{Group, Array}`.

Manifest splitting:

```rust
// icechunk::config
pub struct ManifestConfig {
    pub preload: Option<ManifestPreloadConfig>,
    pub splitting: Option<ManifestSplittingConfig>,
    pub virtual_chunk_location_compression: Option<ManifestVirtualChunkLocationCompressionConfig>,
    pub max_concurrent_manifest_fetches_during_commit: Option<u16>,   // new at 2.2.0, see §6
}
impl ManifestConfig { pub fn empty() -> Self; pub fn splitting(&self) -> &ManifestSplittingConfig; }
// `ManifestConfig` also derives `Default`; `ManifestConfig::default()` and
// `ManifestConfig::empty()` are distinct constructors and both are public.

pub struct ManifestSplittingConfig {
    pub split_sizes: Option<Vec<(ManifestSplitCondition, Vec<ManifestSplitDim>)>>,
}
impl ManifestSplittingConfig { pub fn with_size(split_size: u32) -> Self; }
pub struct ManifestSplitDim { pub condition: ManifestSplitDimCondition, pub num_chunks: u32 }
pub enum ManifestSplitCondition { Or(Vec<_>), And(Vec<_>),
                                  PathMatches { regex: String },
                                  NameMatches { regex: String }, AnyArray }
pub enum ManifestSplitDimCondition { Axis(usize), DimensionName(String), Any }
```

`Repository::lookup_manifest_files(&SnapshotId) -> RepositoryResult<impl
Iterator<Item = ManifestFileInfo>>` reports how many manifests a snapshot has.
With no splitting configured (`ManifestConfig::empty()`) an array's chunk
references go in **one** manifest; with a split size smaller than the array's
chunk count they go in several.

```rust
// icechunk::ops::manifests
pub async fn rewrite_manifests(repository: &Repository, branch: &str, message: &str,
                               max_concurrent_manifests: Option<usize>,
                               properties: Option<SnapshotProperties>,
                               commit_method: CommitMethod) -> ManifestOpsResult<SnapshotId>;
// icechunk::session::CommitMethod { NewCommit, Amend }
```

### 2.5 Milestone 05 — the CLI, commit metadata, storage accounting

The command-line interface itself is §4. Also inherited:

```rust
// icechunk::Repository
pub fn set_default_commit_metadata(&mut self, metadata: SnapshotProperties);
pub fn default_commit_metadata(&self) -> &SnapshotProperties;
// icechunk::format::snapshot
pub type SnapshotProperties = BTreeMap<String, serde_json::Value>;
pub struct SnapshotInfo { pub id: SnapshotId, pub parent_id: Option<SnapshotId>,
                          pub flushed_at: DateTime<Utc>, pub message: String,
                          pub metadata: SnapshotProperties,
                          pub pruned_ancestor_tx_logs: Vec<SnapshotId> }
```

A commit's properties are the repository's default metadata **merged under**
the properties passed to `CommitBuilder::properties`: a key in both takes the
committer's value.

```rust
// icechunk::ops::stats
pub async fn repo_chunks_storage(asset_manager: Arc<AssetManager>,
                                 max_snapshots_in_memory: NonZeroU16,
                                 max_compressed_manifest_mem_bytes: NonZeroUsize,
                                 max_concurrent_manifest_fetches: NonZeroU16)
    -> RepositoryResult<ChunkStorageStats>;
pub struct ChunkStorageStats { pub native_bytes: u64, pub virtual_bytes: u64,
                               pub inlined_bytes: u64 }
```

The three kinds are the three `ChunkPayload` variants: `Ref` counts as native,
`Virtual` as virtual, `Inline` as inlined, each by the payload's own byte
length. A chunk coordinate never written reads back as `None` from
`Session::get_chunk_ref`.

### 2.6 Milestones 07 and 08 — container discipline, dry runs, configuration

A container names the object store it reads through, and that is
`icechunk::ObjectStoreConfig` (re-exported from `icechunk::config`), an enum
whose variants carry the backend's configuration positionally:

```rust
// icechunk::config
pub enum ObjectStoreConfig {
    InMemory,
    LocalFileSystem(PathBuf),          // feature `object-store-fs`
    Http(HttpConfig),                  // feature `object-store-http`
    S3Compatible(S3Options),
    S3(S3Options),
    Gcs(HashMap<String, String>),      // feature `object-store-gcs`
    Azure(HashMap<String, String>),    // feature `object-store-azure`
    Tigris(S3Options),
}
```

```rust
// icechunk::virtual_chunks
pub struct VirtualChunkContainer { pub name: Option<String>, pub store: ObjectStoreConfig,
                                   /* url_prefix is private */ }
impl VirtualChunkContainer {
    pub fn new(url_prefix: String, store: ObjectStoreConfig) -> Result<Self, String>;
    pub fn new_named(name: String, url_prefix: String, store: ObjectStoreConfig) -> Result<Self, String>;
    pub fn url_prefix(&self) -> &str;
}
```

A container's `url_prefix` **must end in `/`**; a prefix that does not is
rejected at construction. That is what keeps `s3://foo/` from authorizing
`s3://foo-production/`.

```rust
// icechunk::config::RepositoryConfig — every field is an Option, and every
// accessor supplies the default.
pub fn merge(&self, other: Self) -> Self;
pub fn set_virtual_chunk_container(&mut self, c: VirtualChunkContainer) -> Result<(), String>;
pub fn virtual_chunk_containers(&self) -> impl Iterator<Item = &VirtualChunkContainer>;
pub fn clear_virtual_chunk_containers(&mut self);
pub fn inline_chunk_threshold_bytes(&self) -> u16;   // default 512
pub fn max_concurrent_requests(&self) -> u16;        // default 256
pub fn manifest(&self) -> &ManifestConfig;
```

`merge` is field by field, *except* that `virtual_chunk_containers` is
**replaced** rather than unioned.

Listing takes a parent: `Session::list_nodes(&Path)` yields the parent node
itself followed by its descendants.

`Store::list_prefix` **rejects** a prefix that is neither the root nor an
existing group or array, with
`StoreErrorKind::BadKeyPrefix { prefix, message }`. `Store::list_dir` does
**not**: given a prefix inside an array's chunk space it walks up to the
closest ancestor array and lists chunk prefixes, and given a prefix with no
ancestor node at all it yields nothing. `list_dir` on an array yields exactly
`zarr.json` and `c`.

### 2.7 Milestone 09 — the Icechunk-2 table of contents

A spec-2 repository keeps its branches, tags, configuration, metadata, status,
feature flags and an operations log in one conditionally-written object, not in
per-reference files.

```rust
// icechunk::Repository
pub async fn ops_log(&self) -> RepositoryResult<(
    impl Stream<Item = RepositoryResult<(DateTime<Utc>, UpdateType, Option<String>)>> + Send,
    Arc<RepoInfo>, storage::VersionInfo)>;
pub async fn get_status(&self) -> RepositoryResult<RepoStatus>;
pub async fn set_status(&self, status: &RepoStatus) -> RepositoryResult<()>;
pub async fn get_metadata(&self) -> RepositoryResult<SnapshotProperties>;
pub async fn set_metadata(&self, metadata: &SnapshotProperties) -> RepositoryResult<()>;
pub async fn update_metadata(&self, metadata: &SnapshotProperties)
    -> RepositoryResult<SnapshotProperties>;
pub async fn feature_flags(&self) -> RepositoryResult<impl Iterator<Item = FeatureFlag>>;
pub async fn rearrange_session(&self, branch: &str) -> RepositoryResult<Session>;

// icechunk::format::repo_info
pub enum UpdateType { RepoInitializedUpdate, RepoMigratedUpdate { .. },
                      RepoStatusChangedUpdate { status },  ConfigChangedUpdate,
                      MetadataChangedUpdate, TagCreatedUpdate { name },
                      TagDeletedUpdate { .. }, BranchCreatedUpdate { name },
                      BranchDeletedUpdate { .. }, BranchResetUpdate { .. },
                      NewCommitUpdate { branch, new_snap_id }, CommitAmendedUpdate { .. }, .. }
pub enum RepoAvailability { Online, ReadOnly }
pub struct RepoStatus { pub availability: RepoAvailability, pub set_at: DateTime<Utc>,
                        pub limited_availability_reason: Option<String> }
pub struct RepoInfo { /* private */ }
impl RepoInfo {
    pub fn find_snapshot(&self, id: &SnapshotId) -> IcechunkResult<SnapshotInfo>;
    pub fn branches(&self) -> IcechunkResult<impl Iterator<Item = (&str, SnapshotId)>>;
    pub fn tags(&self)     -> IcechunkResult<impl Iterator<Item = (&str, SnapshotId)>>;
    pub fn all_snapshots(&self) -> IcechunkResult<impl Iterator<Item = IcechunkResult<SnapshotInfo>>>;
}
// icechunk::feature_flags::FeatureFlag — id(), name(), enabled(), default_enabled()
```

The operations log is **newest first** and records at least the repository's
creation, each commit and each reference change. Setting the status to
`RepoAvailability::ReadOnly` makes `writable_session` fail.

`feature_flags()` is **never empty**: the flag set is a property of the format
generation rather than of anything the repository has been asked to do, so a
freshly created repository already declares its flags. Which flags there are and
what each is called are *out of scope* — only that at least one is reported and
that `FeatureFlag::name()` answers for it (`icechunk-spec/PROFILE.md` P20).

The commit builder:

```rust
// icechunk::session::CommitBuilder
pub fn properties(self, properties: SnapshotProperties) -> Self;
pub fn max_concurrent_nodes(self, n: usize) -> Self;
pub fn allow_empty(self, allow_empty: bool) -> Self;
pub fn amend(self) -> Self;
pub fn anonymous(self) -> Self;
pub fn rewrite_manifests(self) -> Self;
pub fn rebase(self, solver: &(dyn ConflictSolver + Send + Sync), attempts: u16) -> Self;
pub async fn execute(self) -> SessionResult<SnapshotId>;
```

`amend()` replaces the branch tip rather than appending to it, so the ancestry
does not grow. `anonymous()` writes a snapshot that **no branch points at**;
it is still readable through `VersionInfo::SnapshotId`.

Moving a node needs a rearrange session:

```rust
pub async fn move_node(&mut self, from: Path, to: Path) -> SessionResult<()>;
```

- on a writable session: `SessionErrorKind::NonRearrangeSession`;
- an ordinary edit on a rearrange session: `SessionErrorKind::RearrangeSessionOnly`.

### 2.8 Milestone 10 — spec 2.1, additive and compatible both ways

- Expiration records the transaction logs it pruned:
  `SnapshotInfo::pruned_ancestor_tx_logs: Vec<SnapshotId>`, **oldest first**,
  and empty for a snapshot that was never expired past.
- The spec version byte stays `2`: there is no `SpecVersionBin::V2_1`.
  `SpecVersionBin::current() == V2`, `TryFrom<u8>` accepts `1` and `2` and
  rejects anything else.
- Virtual chunk locations are preserved **verbatim** for every scheme except
  `file`: `//`, query strings, fragments and `..` all survive
  `VirtualChunkLocation::from_url(..).url()`. `file://` is a security boundary
  and its path is normalised, so `..` is resolved away.
  `VirtualChunkLocation::is_relative()` and `parse_vcc() -> Option<(&str, &str)>`
  cover the relative `vcc://<container>/<path>` form.
- `icechunk::config::Credentials` carries the explicit no-credential sentinels
  `Credentials::LocalFileSystemAccess` and `Credentials::HttpAccess` beside
  `S3`, `Gcs` and `Azure`.

---

## 3. New in this release

### 3.1 Every `inspect_*` entry point reports the header of the file it read

The four entry points keep their signatures exactly:

```rust
// icechunk::inspect
pub async fn snapshot_json(asset_manager: &AssetManager, id: &SnapshotId, pretty: bool)
    -> RepositoryResult<String>;
pub async fn repo_info_json(asset_manager: &AssetManager, pretty: bool)
    -> RepositoryResult<String>;
pub async fn manifest_json(asset_manager: &AssetManager, manifest_id: &ManifestId, pretty: bool)
    -> RepositoryResult<String>;
pub async fn transaction_log_json(asset_manager: &AssetManager, id: &SnapshotId, pretty: bool)
    -> RepositoryResult<String>;
```

`Repository::asset_manager()` returns `&Arc<AssetManager>`, which derefs to the
`&AssetManager` these take.

What is new is that **all four** documents now carry a top-level `header`
member. The struct behind it is *not* public, so the contract is the **JSON**:

```jsonc
"header": {
  "written_by":  "ic-2.2.0",   // the full implementation string in the file
  "app_name":    "ic",         // written_by up to the first '-'
  "app_version": "2.2.0",      // written_by after the first '-'; OMITTED when absent
  "spec_version": "2.0",       // SpecVersionBin's Display: "1.0" or "2.0"
  "file_type":   "snapshot",   // see below
  "compression": "zstd"        // "none" or "zstd"
}
```

- `written_by` is `FileHeader::impl_name` verbatim, and equals
  `format!("{app_name}-{app_version}")` whenever `app_version` is present.
  For a file this library writes it is `*ICECHUNK_CLIENT_NAME`, i.e.
  `"ic-" + ICECHUNK_LIB_VERSION`.
- `app_version` uses `skip_serializing_if = "Option::is_none"`: the member is
  **absent**, not null, when the implementation string has no `-`.
- `file_type` is one of `"snapshot"`, `"manifest"`, `"transaction-log"`,
  `"repo-info"` — the values of `ICECHUNK_FILE_TYPE_SNAPSHOT`,
  `…_MANIFEST`, `…_TRANSACTION_LOG`, `…_REPO_INFO` — or, for the two file types
  that have no constant, the literals `"attributes"` and `"chunk"`.
- `compression` is `"none"` or `ICECHUNK_COMPRESSION_ZSTD` (`"zstd"`).
- The header a document reports must agree, member for member, with what
  `AssetManager::fetch_*_header` reads out of that same file (§2.3).
- For `transaction_log_json`, `header` describes **the snapshot's own
  transaction-log file only**. When a collapsed ancestry adds a
  `synthetic_composite` member, the merged pruned-ancestor logs have headers of
  their own and they are not reported.

The rest of each document is unchanged. Its top-level members are:

| entry point | top-level JSON members |
| --- | --- |
| `snapshot_json` | `id`, **`header`**, `flushed_at`, `commit_message`, `metadata`, `manifests`, `nodes` |
| `repo_info_json` | **`header`**, `spec_version`, `branches`, `tags`, `deleted_tags`, `snapshots`, `metadata`, `latest_updates`, `repo_before_updates`? |
| `manifest_json` | `id`, **`header`**, `size_bytes`, `num_arrays`, `total_chunk_refs`, `total_inline`, `total_native`, `total_virtual`, `arrays`, `compression` |
| `transaction_log_json` | **`header`**, `new_groups`, `new_arrays`, `deleted_groups`, `deleted_arrays`, `updated_groups`, `updated_arrays`, `updated_chunks`, `moved_nodes`, `synthetic_composite`? |

`pretty = false` renders the same document with **no newline anywhere**;
`pretty = true` renders it indented. Both parse to the same JSON value.
Inspecting an id that was never written is an error, not an empty document.

`manifest_json`'s four `total_*` members — `total_chunk_refs`, `total_inline`,
`total_native`, `total_virtual` — are **counts of chunk references**, never byte
figures, and `total_chunk_refs` is the sum of the other three. The per-array
`num_chunk_refs` / `num_inline` / `num_native` / `num_virtual` members are counts
in the same way, one entry per array under `arrays`. `size_bytes` is a byte
count, not a reference count.

Also new inside `snapshot_json`: each array node carries
`shape: [{ name?, array_length, num_chunks }]`.

The rest of those documents is unchanged from the release that introduced
`inspect_*`, and these are the members the verifier reads besides `header`:

```jsonc
// snapshot_json
"manifests": [ { "id": "<20-char manifest id>", "size_bytes": 0, "num_chunk_refs": 0 } ],
"nodes":     [ { "id": …, "path": "/array", "node_type": "array" | "group",
                 "shape": [ … ],          // arrays only, omitted for a group
                 "manifest_refs": [ … ] } ]  // arrays only
// repo_info_json
"branches": { "<branch name>": "<20-char snapshot id>" },   // and "tags" likewise
// manifest_json
"id": …, "num_arrays": 0, "total_chunk_refs": 0, "total_inline": 0
// transaction_log_json — the six node lists are arrays of path strings
"new_groups": ["/"], "new_arrays": ["/array"], "updated_chunks": [ … ]
```

### 3.2 `ChunkPayload::length`

```rust
// icechunk::format::manifest
impl ChunkPayload {
    /// Number of bytes of chunk data this payload refers to.
    pub fn length(&self) -> ChunkLength;   // ChunkLength = u64
}
```

`Inline(bytes)` → `bytes.len()`; `Virtual(VirtualChunkRef { length, .. })` and
`Ref(ChunkRef { length, .. })` → that `length`.

### 3.3 Deleting a chunk key that cannot exist is a no-op

`Store::delete(&self, key: &str) -> StoreResult<()>` keeps its signature. What
changed is the **error behaviour** on a chunk key. Three cases that used to
surface as `StoreError` now return `Ok(())`:

| key | why it cannot exist |
| --- | --- |
| `array/c/10/1/1` on a 2×2×2 array | the coordinates fall outside the chunk grid |
| `no/such/node/c/0/0/0` | there is no node at that path |
| `c/0` when the root is a group | the key names a group, not an array |

This matches what zarr-python's stores do: deleting a key that is not there is
not an error. Deleting a chunk that *does* exist still deletes it.

**Writing to such a key is still an error.** `Store::set` on a coordinate
outside the grid fails with

```rust
StoreError { kind: StoreErrorKind::SessionError(
                 SessionErrorKind::InvalidIndex { coords, path }), .. }
```

where `path` is the array's path and `coords` the coordinates that were asked
for. `Session::set_chunk_ref` fails the same way with
`SessionErrorKind::InvalidIndex { coords, path }` directly.

`StoreErrorKind` also has its own, different `InvalidIndex { axis, coords,
num_chunks }` used by the listing paths; the write path is the `SessionError`
one above.

### 3.4 A zero-length chunk write is rejected, whatever the reference kind

No valid chunk is ever zero bytes long: a chunk has to decode to the full chunk
shape, and nothing decodes from nothing. So a payload whose `length()` is `0` is
rejected before anything else, for **all three** reference kinds:

```rust
// icechunk::session::SessionErrorKind
ZeroLengthChunk { coords: ChunkIndices, path: Path },
```

- `ChunkPayload::Inline(Bytes::new())`
- `ChunkPayload::Virtual(VirtualChunkRef { length: 0, .. })`
- `ChunkPayload::Ref(ChunkRef { length: 0, .. })`

```rust
// icechunk::format::manifest — the materialized reference, all fields pub
pub struct ChunkRef { pub id: ChunkId, pub offset: ChunkOffset, pub length: ChunkLength }
```

reached through `Session::set_chunk_ref(path, coords, Some(payload))`, and
therefore also through `Store::set` at a chunk key with an empty value. `coords`
and `path` in the error are the ones that were asked for.

`data: None` is untouched: **deleting** is how "this chunk is not stored at all"
is recorded, and such a chunk reads back as the array's fill value. So
`set_chunk_ref(path, coords, None)` and `delete_chunks(..)` still succeed, and a
non-empty payload is still accepted.

This is a deliberate asymmetry with a plain key-value store, which would accept
an empty value at any key.

### 3.5 The object-metadata key rename

Object-metadata keys use letters only: Azure needs a valid C# identifier, which
bars `-`, and nginx-fronted S3 gateways drop header names containing `_` — which
failed writes with `AccessDenied: There were headers present in the request
which were not signed`. The constant **names** are unchanged; their **values**
changed, and a `*_DEPRECATED` constant now names the old spelling beside each.

All eight live in `icechunk::format::format_constants` and are `pub`:

| meaning | constant | value | deprecated constant | value |
| --- | --- | --- | --- | --- |
| spec version | `LATEST_ICECHUNK_FORMAT_VERSION_METADATA_KEY` | `icspecver` | `…_METADATA_KEY_DEPRECATED` | `ic_spec_ver` |
| client name | `ICECHUNK_CLIENT_NAME_METADATA_KEY` | `icclient` | `…_METADATA_KEY_DEPRECATED` | `ic_client` |
| file type | `ICECHUNK_FILE_TYPE_METADATA_KEY` | `icfiletype` | `…_METADATA_KEY_DEPRECATED` | `ic_file_type` |
| compression | `ICECHUNK_COMPRESSION_METADATA_KEY` | `iccompalg` | `…_METADATA_KEY_DEPRECATED` | `ic_comp_alg` |

Every new spelling starts with an ASCII letter and is entirely ASCII
alphanumeric. Every deprecated spelling contains `_`.

**Scope, stated plainly.** These four keys are *write-only observability
metadata*: the authoritative header lives in the file bytes and is read by
`parse_file_header` (§2.3). In this release **no parse path anywhere in the tree
reads either spelling** — the `*_DEPRECATED` constants are defined and never
consumed. "Readable under both spellings" is therefore observable only as the
constants' values, and that is all this milestone checks. No requirement group makes
the library parse object metadata under either spelling.

Unchanged in the same module, values included:
`ICECHUNK_FILE_TYPE_SNAPSHOT = "snapshot"`, `…_MANIFEST = "manifest"`,
`…_TRANSACTION_LOG = "transaction-log"`, `…_REPO_INFO = "repo-info"`,
`ICECHUNK_COMPRESSION_ZSTD = "zstd"`, and
`ICECHUNK_CLIENT_NAME = "ic-" + ICECHUNK_LIB_VERSION`.

The fifth renamed key, the conditional-PUT write id
(`icechunk_write_id` → `icechunkwriteid`), lives in `icechunk_storage::readback`,
which `icechunk::storage` does **not** re-export. It is out of scope: see §7.

### 3.6 Garbage collection is safe under clock skew

No public signature changed. What changed is which clock the retention decision
uses: the repository-info retention deadline is now compared against the
**object store's `created_at`** for the snapshot file — the same clock
`garbage_collect` uses to decide whether to delete that file — instead of
against the host-stamped `SnapshotInfo::flushed_at`.

The observable consequence, end to end:

1. build a history where an expiration has left a retained snapshot carrying
   `pruned_ancestor_tx_logs`;
2. take that snapshot's `created_at` from `AssetManager::list_snapshots()` and
   check it is strictly later than its `flushed_at` in the repository info — the
   `(flushed_at, created_at]` window;
3. run `garbage_collect` with **both** `GCConfig::clean_all` cutoffs set to that
   `created_at`.

Then: the snapshot is **retained** — it is still in the repository info, its
`pruned_ancestor_tx_logs` are intact and every transaction log they name is
still fetchable — so `GCSummary::transaction_logs_deleted == 0`, while the
genuinely released snapshot's file is still collected.

A snapshot that the storage listing does not show yet cannot be deleted by that
run either, so it is retained.

### 3.7 The Hugging Face storage preset

```rust
// icechunk::storage  (feature `s3`, which is on by default)
pub fn new_hf_storage(
    config: S3Options,
    bucket: String,
    prefix: Option<String>,
    namespace: &str,
    credentials: Option<S3Credentials>,
    extra_read_headers: Vec<(String, String)>,
    extra_write_headers: Vec<(String, String)>,
    legacy_rooted_keys: Option<bool>,
) -> StorageResult<Arc<dyn Storage + Send + Sync>>;
```

`namespace` is the Hugging Face user or organization that owns the bucket. The
preset:

- sets `endpoint_url` to **`https://s3.hf.co/<namespace>`**, overwriting any
  endpoint in `config` — Hugging Face runs one gateway. There is **no trailing
  slash** in the recorded endpoint;
- sets `region` to **`us-east-1`** *only when `config.region` is `None`*; an
  explicit region is kept;
- sets `force_path_style` to **`true`** — the gateway does not serve
  virtual-hosted style URLs;
- sends the **bare bucket name**: the namespace lives only in the endpoint path;
- defaults `credentials` to `S3Credentials::FromEnv` when it is `None`, and
  builds a **writable** storage.

Construction is **pure**: no socket, no DNS, no credential read. The client is
built lazily, and `S3Credentials::FromEnv` reads nothing at construction time.

**How a test observes the configuration.** `S3Storage`'s `config` field is
private, so the observation point is the `Storage` trait's synchronous
`storage_info()`:

```rust
// icechunk::storage
pub struct StorageInfo { pub backend_type: &'static str,
                         pub fields: Vec<(&'static str, String)> }
pub trait Storage {
    fn storage_info(&self) -> StorageInfo;
    async fn can_write(&self) -> StorageResult<bool>;   // async_trait
    /* … */
}
```

`can_write()` is the observation point for "writable": it reads a flag the
constructor set and touches nothing, so it answers offline. For the storage
`new_hf_storage` returns it is `true`.

For the storage `new_hf_storage` returns, `backend_type == "S3 (native)"` and
`fields` contains `("bucket", …)`, then `("prefix", …)` **only when the prefix
is non-empty**, then the `S3Options` fields: `("region", …)`,
`("endpoint_url", …)`, `("force_path_style", "True")`, and `("anonymous",
"True")` / `("allow_http", "True")` / `("requester_pays", "True")` when those
are set. **Every boolean field is reported only when it is true**, as the
literal `"True"`, and is **omitted from `fields` altogether when it is false**:
there is no `"False"` rendering. `region` and `endpoint_url` likewise appear
only when they are set.

**A documented consistency limitation of this gateway, not of the preset.** The
Hugging Face gateway discards user metadata, so a conditional write whose
response is lost in transit can report a conflict that never happened; the
documented remedy is to retry the commit. Nothing offline can observe this and
no requirement group asserts it.

---

## 4. The command-line interface

### 4.1 Binary, feature and build

```toml
[[bin]]
name = "icechunk"
path = "src/bin/icechunk/main.rs"
required-features = ["cli"]
```

- Binary name: **`icechunk`**, the only `[[bin]]` target in the workspace.
- The `cli` feature is **not** in `default`. Build it with:

  ```sh
  cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk
  ```

- `icechunk::cli` is a public module that is empty without the feature; with it
  it exposes `cli::config` and `cli::interface`.
- `icechunk::cli::interface::IcechunkCLI` is `pub` and derives `clap::Parser`;
  its field and every subcommand type are private, so the only library entry
  points are `IcechunkCLI::parse()` / `try_parse_from` and
  `pub async fn run_cli(args: IcechunkCLI) -> anyhow::Result<()>`. Everything
  else goes through the process boundary.

### 4.2 Argument grammar

```
icechunk <SUBCOMMAND>

icechunk config init [-f | --force]
icechunk config add <alias>
icechunk config list

icechunk repo create <alias>

icechunk ancestry <alias> [reference] [-n <N>]
icechunk branch list   <alias>
icechunk branch create <alias> <branch> <from>
icechunk branch delete <alias> <branch>
icechunk tag    list   <alias>
icechunk tag    create <alias> <tag> <target>
icechunk tag    delete <alias> <tag>
```

- Every positional is required except `ancestry`'s `reference`, which defaults
  to `main`.
- `-n` on `ancestry` is **short only** (there is no `--n`), parses as `usize`
  and defaults to `10`. `-n 0` lists nothing.
- `<alias>` is any string; a missing alias is not a parse error, it fails later
  when the repository is opened.
- `ancestry`, `branch` and `tag` are **new in this release**;
  `icechunk snapshot list` was **removed** and is no longer a subcommand.
- `config init` and `config add` are interactive (they prompt on a TTY) and are
  not usable non-interactively. No requirement group runs them.

`<reference>`, `<from>` and `<target>` are resolved in this order:

1. a raw snapshot id (20 Crockford base-32 characters),
2. a branch name,
3. a tag name,

and otherwise fail with a message naming the string that could not be resolved.
A branch or tag created from a reference starts at **exactly** the snapshot that
reference resolves to, so `icechunk branch create demo feature main` leaves
`feature` and `main` pointing at the same snapshot id.

### 4.3 Exit statuses

| status | when |
| --- | --- |
| `0` | the command succeeded; its output, if any, is on **stdout** |
| `0` | `--help` / `-h` early exit; the help text goes to **stdout**. The top-level help lists the subcommands §4.2 defines, and `branch --help` / `tag --help` each list `list`, `create` and `delete` |
| `1` | the command ran and failed (no such alias, unresolvable reference, cannot delete `main`, tag already exists, …). The message goes to **stderr**; stdout stays empty |
| `2` | the arguments did not parse: unknown subcommand, missing required positional, `-n` that is not a number. Usage goes to **stderr**; stdout stays empty |

`run_cli`'s error message always names the thing that failed — the alias, the
branch, the tag or the unresolvable reference — but the **exact prose is not
part of this contract** and no requirement group matches it.

### 4.4 stdout format — never JSON

Nothing the CLI prints is JSON. `icechunk::inspect`'s JSON is reachable from
the library only.

```
# branch list                — one line per branch
<SNAPSHOT_ID>  <branch-name>          # two spaces between the two columns

# tag list                   — one line per tag, same shape
<SNAPSHOT_ID>  <tag-name>

# ancestry                   — one block per snapshot, newest first,
#                              each followed by a blank line
Snapshot: <SNAPSHOT_ID>
Date: <flushed_at, local time zone>

  <commit message>                    # preceded by a blank line; omitted when empty

# repo create / branch create / branch delete / tag create / tag delete
#   one confirmation line naming what was done. `tag create` names the RESOLVED
#   snapshot id, not the reference as typed.

# config list                — the configuration as YAML, then a blank line.
#   `RepositoryDefinition` is externally tagged, so the variant is a YAML TAG
#   and never a nested mapping — see §4.5, which is the authority on the shape.
#   `config:` is the serde form of `RepositoryConfig`, so every field appears,
#   an unset one as `null`.
repos:
  <alias>: !LocalFileSystem
    path: …
    config:
      inline_chunk_threshold_bytes: null
      …                                 # one line per RepositoryConfig field
```

`<SNAPSHOT_ID>` is always 20 Crockford base-32 characters. The confirmation
lines' wording, punctuation and any decoration are **not** part of this
contract; their being on stdout, and the exit status, are.

### 4.5 The configuration file

`run_cli` always begins by loading the configuration, and **a load failure is
swallowed**: a missing or unparseable file yields an empty repository map, after
which any command naming an alias fails with status `1`.

- Directory: `$XDG_CONFIG_HOME` if set, else `$HOME/.config`.
- Path: `<that>/icechunk/cli-config.yaml`.

The YAML is the serde form of

```rust
// icechunk::cli::config
pub struct CliConfig { pub repos: HashMap<RepositoryAlias, RepositoryDefinition> }
pub struct RepositoryAlias(pub String);
pub enum RepositoryDefinition {
    LocalFileSystem { path: PathBuf, config: RepositoryConfig },
    S3     { location: RepoLocation,      object_store_config: S3Options,
             credentials: S3Credentials,  config: RepositoryConfig },
    Tigris { location: RepoLocation,      object_store_config: S3Options,
             credentials: S3Credentials,  config: RepositoryConfig },
    Azure  { location: AzureRepoLocation, object_store_config: HashMap<String, String>,
             credentials: AzureCredentials, config: RepositoryConfig },
    GCS    { location: RepoLocation,      object_store_config: HashMap<String, String>,
             credentials: GcsCredentials, config: RepositoryConfig },
}
```

`RepositoryDefinition` is an **externally tagged** enum, and `serde_yaml_ng`
renders an externally tagged enum as a **YAML tag** — `!LocalFileSystem` — not
as a nested single-key mapping. So a local-filesystem repository is declared as

```yaml
repos:
  demo: !LocalFileSystem
    path: /somewhere/repo
    config: {}
```

`config: {}` is a `RepositoryConfig` with every field unset. The file the CLI
writes itself, and the file it reads, are the same shape: `icechunk config list`
round-trips this document.

`icechunk repo create demo` must then create a repository at that path that
`Repository::open` can subsequently open, with `main` as its only branch. This
is the one place the verifier looks at the object-store layout at all: after
`repo create`, `<path>/repo` is a file and `<path>/snapshots/` is a non-empty
directory (`icechunk-spec/PROFILE.md` P5). Nothing else below the `Storage`
trait is ever inspected.

---

## 5. The other object-store presets

Peripheral to this milestone, but part of the same public surface. All are
constructible offline; none opens a socket, because every
client is built lazily.

```rust
// icechunk::storage
pub fn new_s3_storage(config: S3Options, bucket: String, prefix: Option<String>,
                      credentials: Option<S3Credentials>,
                      extra_read_headers: Vec<(String, String)>,
                      extra_write_headers: Vec<(String, String)>,
                      legacy_rooted_keys: Option<bool>)
    -> StorageResult<Arc<dyn Storage + Send + Sync>>;
pub fn s3_storage(/* the same eight */) -> StorageResult<S3Storage>;

pub fn new_r2_storage(config: S3Options, bucket: Option<String>, prefix: Option<String>,
                      account_id: Option<String>, credentials: Option<S3Credentials>,
                      extra_read_headers: Vec<(String, String)>,
                      extra_write_headers: Vec<(String, String)>,
                      legacy_rooted_keys: Option<bool>)
    -> StorageResult<Arc<dyn Storage + Send + Sync>>;
pub fn r2_storage(/* the same */) -> StorageResult<S3Storage>;

pub fn new_tigris_storage(config: S3Options, bucket: String, prefix: Option<String>,
                          credentials: Option<S3Credentials>, use_weak_consistency: bool,
                          extra_read_headers: Vec<(String, String)>,
                          extra_write_headers: Vec<(String, String)>,
                          legacy_rooted_keys: Option<bool>)
    -> StorageResult<Arc<dyn Storage + Send + Sync>>;
pub fn tigris_storage(/* the same */) -> StorageResult<S3Storage>;

pub async fn new_s3_object_store_storage(config: S3Options, bucket: String,
                                         prefix: Option<String>,
                                         credentials: Option<S3Credentials>,
                                         extra_read_headers: Vec<(String, String)>,
                                         extra_write_headers: Vec<(String, String)>)
    -> StorageResult<Arc<dyn Storage + Send + Sync>>;
pub fn new_gcs_storage(bucket: String, prefix: Option<String>,
                       credentials: Option<GcsCredentials>,
                       config: Option<HashMap<String, String>>,
                       extra_read_headers: Vec<(String, String)>,
                       extra_write_headers: Vec<(String, String)>)
    -> StorageResult<Arc<dyn Storage + Send + Sync>>;
```

Each records its bucket, prefix and `S3Options` through `storage_info()` as in
§3.7. The two gateway presets set their own endpoint, region and path style,
exactly as `new_hf_storage` does for Hugging Face:

| preset | `endpoint_url` | `region` when none was given | `force_path_style` |
| --- | --- | --- | --- |
| `new_tigris_storage` | `https://t3.storage.dev` | Tigris requires one: a missing region is an **error**, not a default, unless `use_weak_consistency` is `true` | left as given |
| `new_r2_storage` | `https://<account_id>.r2.cloudflarestorage.com` | `auto` | `true` |

`new_r2_storage` needs a bucket (or a prefix it can split one out of) **and**
either an `account_id` or an explicit `endpoint_url`; without them it fails.

`S3Options` is `#[non_exhaustive]`, so it is built as
`S3Options::default().with_region(..).with_endpoint_url(..).with_allow_http(..)
.with_force_path_style(..).with_anonymous(..).with_requester_pays(..)`. Each
builder consumes and returns `Self`; the two string ones take
`impl Into<String>`, so a `&str` literal is what a caller passes:

```rust
// icechunk::config::S3Options — the fields are public and directly assignable
// on an owned value, which is how a caller sets one without a builder.
pub region: Option<String>,      pub endpoint_url: Option<String>,
pub anonymous: bool,             pub allow_http: bool,
pub force_path_style: bool,      pub requester_pays: bool,
/* … */
impl S3Options {
    pub fn with_region(self, region: impl Into<String>) -> Self;
    pub fn with_endpoint_url(self, endpoint_url: impl Into<String>) -> Self;
    pub fn with_anonymous(self, anonymous: bool) -> Self;
    pub fn with_allow_http(self, allow_http: bool) -> Self;
    pub fn with_force_path_style(self, force_path_style: bool) -> Self;
    pub fn with_requester_pays(self, requester_pays: bool) -> Self;
}
```

```rust
// icechunk::config
pub enum S3Credentials { FromEnv, Anonymous, Static(S3StaticCredentials),
                         Refreshable(Arc<dyn S3CredentialsFetcher>) }
pub struct S3StaticCredentials {          // all fields pub
    pub access_key_id: String,
    pub secret_access_key: String,
    pub session_token: Option<String>,
    pub expires_after: Option<DateTime<Utc>>,
}
// icechunk::storage — the GCS preset's own credential enum, same four shapes
pub enum GcsCredentials { FromEnv, Anonymous, Static(GcsStaticCredentials),
                          Refreshable(Arc<dyn GcsCredentialsFetcher>) }
```

Per-role clients:

```rust
// icechunk::storage
pub enum Role { Read, Write }                 // Debug, Clone, Copy, PartialEq, Eq
pub fn validate_extra_headers(headers: &[(String, String)]) -> Result<(), StorageError>;

pub trait ObjectStoreBackend: Debug + Display + Sync + Send {
    fn mk_object_store(&self, settings: &Settings, role: Role)
        -> Result<Arc<dyn ObjectStore>, StorageError>;
    fn read_write_headers_differ(&self) -> bool { false }
    fn prefix(&self) -> String;
    fn storage_info(&self) -> StorageInfo;
    fn default_settings(&self) -> Settings;
    /* … */
}

pub struct S3ObjectStoreBackend {             // all fields pub
    pub bucket: String, pub prefix: Option<String>,
    pub credentials: Option<S3Credentials>, pub config: Option<S3Options>,
    pub extra_read_headers: Vec<(String, String)>,
    pub extra_write_headers: Vec<(String, String)>,
}
```

`Settings` is `icechunk::storage::Settings`, the per-repository storage tuning
struct; it implements `Default`, and `Settings::default()` is what a caller
without a repository at hand passes to `mk_object_store`.

`mk_object_store` must succeed for both roles offline. `read_write_headers_differ()`
is `false` when the two header vectors are equal and `true` when they differ.

Both `S3ObjectStoreBackend` and `GcsObjectStoreBackend` derive `Serialize` and
`Deserialize`, and both gained the two header vectors at this release carrying
them as `#[serde(default, skip_serializing_if = "Vec::is_empty")]` — so a
backend serialised by an earlier release, whose JSON has neither field, still
deserialises with both vectors empty, and an empty vector is still omitted on
the way out:

```rust
// icechunk::storage
pub struct GcsObjectStoreBackend {        // all fields pub
    pub bucket: String, pub prefix: Option<String>,
    pub credentials: Option<GcsCredentials>,
    pub config: Option<HashMap<GoogleConfigKey, String>>,
    pub extra_read_headers: Vec<(String, String)>,
    pub extra_write_headers: Vec<(String, String)>,
}
```
`S3ObjectStoreBackend::storage_info().backend_type == "S3"` (the object-store
backend), distinct from `"S3 (native)"` (the native `S3Storage`).
`validate_extra_headers` accepts valid HTTP header name/value pairs and rejects
invalid ones — a name containing a space, a value containing a newline.

---

## 6. Migration from the previous checkpoint

Everything here is a **breaking change for a caller written against v2.1.0**.
A candidate that inherits a v2.1.0 workspace has to make these edits.

### 6.1 Removed

| item | where |
| --- | --- |
| the CLI subcommand `icechunk snapshot list` (and its `-b/--branch` flag) | `icechunk::cli::interface` |

No `pub` item was removed from any library crate.

### 6.2 Eight storage constructors gained two header parameters

`extra_read_headers: Vec<(String, String)>` and
`extra_write_headers: Vec<(String, String)>` are inserted **after
`credentials`** (after `use_weak_consistency` for Tigris) and **before
`legacy_rooted_keys`**, in:

`new_s3_storage`, `s3_storage`, `new_r2_storage`, `r2_storage`,
`new_tigris_storage`, `tigris_storage`, `new_s3_object_store_storage`,
`new_gcs_storage`. The full signatures are in §5.

Unchanged: `new_azure_blob_storage`, `new_http_storage`,
`new_local_filesystem_storage`, `new_in_memory_storage`, `new_redirect_storage`,
`mk_client`, and `S3Storage::new` (which already took both vectors).

### 6.3 `ObjectStoreBackend::mk_object_store` gained a `Role`

```diff
- fn mk_object_store(&self, settings: &Settings) -> Result<Arc<dyn ObjectStore>, StorageError>;
+ fn mk_object_store(&self, settings: &Settings, role: Role)
+     -> Result<Arc<dyn ObjectStore>, StorageError>;
+ fn read_write_headers_differ(&self) -> bool { false }   // new, defaulted
```

Every implementor moves with it. `S3ObjectStoreBackend` and
`GcsObjectStoreBackend` each gained `extra_read_headers` and
`extra_write_headers` (both `#[serde(default, skip_serializing_if =
"Vec::is_empty")]`, so an older serialised backend still deserialises).
`AzureObjectStoreBackend` and `HttpObjectStoreBackend` are unchanged.

### 6.4 `rewrite_manifests`' concurrency became optional

```diff
- max_concurrent_manifests: usize,
+ max_concurrent_manifests: Option<usize>,
```

`None` means "take it from
`ManifestConfig::max_concurrent_manifest_fetches_during_commit()`".

### 6.5 New configuration field and accessor

```rust
// icechunk::config::ManifestConfig
#[serde(default)] pub max_concurrent_manifest_fetches_during_commit: Option<u16>,
/// The effective manifest update concurrency, always at least 1.
pub fn max_concurrent_manifest_fetches_during_commit(&self) -> u16;
```

The field defaults to `None` and the accessor then reports **1** (serial).
```

`CommitBuilder::max_concurrent_nodes(0)` is now clamped to `1` rather than
silently dropping every manifest, and the builder's default comes from that
accessor.

### 6.6 The metadata key values changed

See §3.5. Constant names are unchanged; their string values are not, and four
`*_DEPRECATED` constants were added beside them.

### 6.7 New error variants

```rust
// icechunk::session::SessionErrorKind — both new
ZeroLengthChunk { coords: ChunkIndices, path: Path },
UnsupportedChunkGridForReindex { path: Path, grid_type: String },
```

`SessionErrorKind` is `#[non_exhaustive]`, so adding variants is compatible.

### 6.8 Dependency bump crossing the public API

`object_store` moved **0.13.2 → 0.14.0**. It appears in public signatures
(`ObjectStoreBackend::mk_object_store`'s return type, `ObjectStorage`,
`ClientConfigKey`, `AzureConfigKey`, `GoogleConfigKey`, `HeaderMap`,
`ClientOptions`), so the workspace must move with it. `quick_cache` moved
0.6.22 → 0.7.0 and `syn` 2.0 → 3.0; neither crosses the public API.

---

## 7. What is out of scope

**Anything below the documented boundary.** Module layout, private item names,
the physical object-store key layout, the on-disk byte encoding beyond what §2.3
and §2.4 state, allocation, logging, timing and the candidate's own test
targets.

**Message wording.** Every error assertion is on a **category** — the enum
variant and the payload this document names for it — never on a `Display` or
`Debug` rendering, a source chain or a message string. For the CLI, the
assertions are on argv acceptance, exit status and a structural property of
stdout; the prose of a confirmation line or an error line is never matched.

**Ordering that this document does not fix.** Where a collection's order is not
stated here, none is required and every driver sorts before comparing. The
orders that *are* fixed: `Repository::ancestry` is newest first;
`Repository::ops_log` is newest first; `pruned_ancestor_tx_logs` is oldest
first.

**The S3 endpoint-with-path fix.** This release appends the `/` that separates
an endpoint path from the bucket name, so an endpoint such as
`https://s3.hf.co/<ns>` stops addressing `/<ns><bucket>/<key>` and failing with
`NoSuchBucket`, while endpoints without a path (Tigris, R2, MinIO) are
unchanged. The function that does it is **private**, and the only caller builds
a live AWS SDK client, so **there is no offline public observation point** and
nothing here checks it. The truth table it must satisfy is recorded in
`icechunk-spec/PROFILE.md` (P24) for reference. What *is* in scope is the input
side: `new_hf_storage` records `endpoint_url` as `https://s3.hf.co/<namespace>`
with **no** trailing slash, because the slash is added when the client is built,
not when the configuration is made.

**The conditional-PUT write-id constants.** `WRITE_ID_METADATA_KEY`
(`icechunkwriteid`) and `WRITE_ID_METADATA_KEY_DEPRECATED`
(`icechunk_write_id`) live in `icechunk_storage::readback`, which
`icechunk::storage` does not re-export and which the driver crate does not
depend on. Out of scope.

**Zero-copy buffer writes.** This release stops copying the NumPy buffer on
`set` / `set_if_not_exists`. That is a **Python-layer** change:
`icechunk::store::Store::set(&self, key: &str, value: Bytes)` is unchanged, and
so is `icechunk-python/src/store.rs`. **No Rust test can observe it**, and none
does.

**The Python, JavaScript and WebAssembly bindings.** `icechunk-python` and
`icechunk-js` must remain workspace members and the workspace must build, but
nothing in either is in scope.

**Anything that needs a network, a cloud credential or a container.** Every
requirement group runs offline over in-memory or local-filesystem storage. The cloud
presets are constructed and read back through `storage_info()` and never
connected. A group that needed MinIO, Azurite, a GCS emulator or a live
Hugging Face bucket would be unrunnable here, and none exists.

**Interactive CLI commands.** `icechunk config init` and `icechunk config add`
prompt on a TTY; no requirement group runs them.

**Rebuilding history.** Nothing checks how many snapshots a repository squashes
into, how large a manifest is, or how many objects a garbage collection touches
beyond the counters `GCSummary` names.

**Manifest and lockfile facts.** §1's workspace identity — that every member
crate is at `2.2.0`, that `Cargo.lock` is committed at the root and consistent,
that `icechunk` is the only `[[bin]]` target, and §6.8's `object_store` 0.14.0 /
`quick_cache` 0.7.0 / `syn` 3.0 pins — lives in manifests and lockfiles. The
driver crate sees `icechunk::` and nothing below it, so reaching any of these
would mean a driver parsing a manifest. What *is* in scope is that both build
commands in §1 succeed, which is what those facts exist to make true.

**`icechunk::cli`'s Rust types.** `IcechunkCLI`, `run_cli`, `cli::config` and
`cli::interface` are behind the `cli` feature, which the driver crate does not
enable, so the module is empty for it. Only the **binary** is in scope, through
argv, exit status and stdout, exactly as §4 describes it.

**The non-local configuration-file variants.** §4.5 lists all five
`RepositoryDefinition` variants and their fields, and every one must
deserialise. Only `!LocalFileSystem` is exercised: the other four name cloud
repositories that cannot be opened offline, and writing their YAML by hand needs
the *internal* serde tags of `S3Credentials`, `AzureCredentials` and
`GcsCredentials`, which are implementation detail this document does not fix.

**`app_version` being omitted when the implementation string has no `-`.** The
rule in §3.1 is normative and an implementation must follow it, but every file
this library writes carries `ICECHUNK_CLIENT_NAME`, which always contains a `-`,
and nothing public writes a file under a different implementation name. There is
no offline observation point and no group asserts one.

**Two error variants that exist but cannot be provoked from outside.**
`SessionErrorKind` is `#[non_exhaustive]`, so a driver can neither construct nor
match-construct one. §6.7's `UnsupportedChunkGridForReindex` must exist, but
only a rectilinear-grid reindex produces it and no requirement group reaches that.
`StoreErrorKind::InvalidIndex { axis, coords, num_chunks }` (§3.3) is likewise
produced only by the chunk-size listing path inside `icechunk::store`; the
sentence in §3.3 is there to distinguish it from the **write** path's
`SessionErrorKind::InvalidIndex`, which *is* in scope, not to add an obligation.

**"A snapshot the storage listing does not show yet is retained too" (§3.6).**
Observing this needs a `Storage` that hides an object from `list` while `get`
still serves it. `Storage` is sealed (`icechunk-spec/PROFILE.md` P13), so no
driver can build one, and no group asserts it. The rest of §3.6 is in scope end to
end.
