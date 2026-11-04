# TEST_INTERFACE.md — milestone 05 (CLI, expiration accounting and stores)

This is the public contract the hidden verifier holds you to. Everything the
verifier names — crate identity, build commands, module paths, exported
symbols, signatures, the command-line grammar and its exit statuses, the
formats crossing the boundary, and the pinned crates it links against — appears
here or in a section of `icechunk-spec/` named from `instruction.md`'s reading
list. The verifier may combine these in ways this document does not enumerate;
it will not use an interface this document does not name.

This document is **self-contained**: it restates the inherited surface from
milestones 01–04 as *this release* spells it, and then everything new. Where a
signature changed since milestone 04, section 6 lists the change and the form
below is the required one.

Section 9 lists what is **out of scope**. Read it: several things the Icechunk
documentation describes are deliberately outside this milestone's contract.

---

## 1. Crate identity and build contract

| Item | Value |
| --- | --- |
| Workspace manifest | `/app/Cargo.toml` — `members = ["icechunk", "icechunk-python"]`, `default-members = ["icechunk"]`, `resolver = "2"` |
| Crate under test | `icechunk` at `/app/icechunk` |
| Crate manifest | `/app/icechunk/Cargo.toml` |
| Package version | `0.2.11` — the Rust crate version, which is **not** the upstream tag number |
| Rust edition | `2024` (moved up from `2021`; needs rustc ≥ 1.85) |
| Crate root | `/app/icechunk/src/lib.rs` |
| Lockfile | `/app/Cargo.lock` — present, committed, consistent with the manifests |
| Library build | `cargo build` run in `/app` |
| Binary build | `cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk` |
| Toolchain | the `rustc`/`cargo` in the image (1.98); no nightly features |

The verifier runs `cargo build` in `/app` first. If that fails, every requirement
group of this milestone fails. It then runs the **binary build** above; if only
that fails, the command-line groups fail and the library groups still run.

After building, the verifier creates a **throw-away driver crate outside
`/app`** that declares `icechunk = { path = "/app/icechunk" }` and compiles its
own integration tests against your crate. Your manifests are never modified and
no test of yours is ever run or read as evidence.

The driver crate declares **no features** on `icechunk`, so everything in
section 4 must be reachable from a default-feature build of the library. Only
the compiled binary exercises the `cli` feature.

### 1.1 The `cli` feature and the `icechunk` binary

`/app/icechunk/Cargo.toml` must declare, at minimum:

```toml
[features]
# EXTEND this block; do not replace it. This manifest already declares a
# `logs` feature, and `icechunk-python/Cargo.toml` depends on `icechunk` with
# `features = ["logs"]`. `icechunk-python` is a workspace member, so `cargo
# build` at /app resolves it even though `default-members` keeps it from being
# built - dropping `logs` breaks workspace resolution and with it every group.
logs = [ /* whatever it already names */ ]

# Name the optional dependencies your CLI needs, and prefer the `dep:`
# namespace - `cli = ["dep:clap", ...]` - so that an implicit feature is not
# created for each of them and the verifier's own driver crate, which enables
# no feature of `icechunk`, resolves without them. Which dependencies the list
# names is yours; nothing about its contents is in scope.
cli = [ /* "dep:<your cli-only dependencies>" */ ]

[[bin]]
name = "icechunk"
path = "src/bin/icechunk/main.rs"   # the path is yours; the name is not
required-features = ["cli"]
```

Consequences the verifier depends on:

- there is **no `default` feature**, so a plain `cargo build` at `/app` builds
  the library only and produces **no binary**;
- the binary target is named **`icechunk`**, so the build above puts it at
  `$CARGO_TARGET_DIR/debug/icechunk`. The verifier redirects
  `CARGO_TARGET_DIR` and passes the resulting path to its drivers in the
  environment variable **`ICECHUNK_TEST_CLI_BIN`**;
- `pub mod cli;` is unconditional in `lib.rs`, but its contents may be gated on
  the feature. The library groups never name anything under `icechunk::cli`.

## 2. Workspace layout

```text
/app/
  Cargo.toml              workspace manifest        (required)
  Cargo.lock              resolved graph            (required)
  icechunk/
    Cargo.toml            the crate under test      (required, name = "icechunk")
    src/lib.rs            crate root
    src/...               your implementation
  icechunk-python/        carried forward; never built by the verifier
  icechunk-spec/          the normative corpus (read-only)
  TEST_INTERFACE.md       this file
  WORKSPACE_HANDOFF.md    what to preserve when this milestone ends
  timer.sh                advisory countdown for this milestone's budget
```

Module organisation under `icechunk/src/` is yours **except** for the module
paths section 4 names: those are part of the public API and the drivers spell
them verbatim.

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

- `bytes::Bytes`, `chrono::DateTime<chrono::Utc>`, `futures::Stream`,
  `serde_json::Value` and `tokio::sync::RwLock` cross the API boundary, so your
  crate must depend on **semver-compatible** versions of those crates. A
  different major version makes the types distinct and the drivers will not
  compile.
- Async entry points are driven by `tokio`. Every `async fn` this document
  names must run on a plain multi-threaded tokio runtime; do not require a
  particular flavour or a runtime the caller must install.
- The drivers unwrap your `Result` and format failures with `{}` and `{:?}`, so
  every error type must implement `std::fmt::Debug` and `std::fmt::Display` and
  must satisfy `std::error::Error`. Where section 4 annotates a type with
  standard traits, those impls are required as well. Section 9 excludes the
  **text** those impls produce; it never excludes the impls themselves. This
  binds the `…ErrorKind` enums of section 4.1 as well as the `ICError<…>`
  aliases over them: the drivers **match on `err.kind`** and, when a match
  fails, format **the kind** with `{:?}`, so every `…ErrorKind` enum must
  implement `std::fmt::Debug` in its own right.
- The drivers also assert failures with `Result::expect_err`, which panics by
  formatting the **successful** value with `{:?}`: it bounds the `Ok` type, not
  the error type. Every `Ok` type used that way at this milestone is a standard
  or dependency type, or one section 4 already requires `Debug` of — except
  `Repository` (section 4.3), which this document does not otherwise annotate,
  and `format::snapshot::NodeSnapshot`, whose section-4.2 derive line carries
  `Debug` for exactly this reason. Both must implement `std::fmt::Debug`.
- The drivers use `tempfile::TempDir` for scratch directories, so the
  local-filesystem backend must accept plain absolute paths.

---

## 4. Public API required at this milestone

### 4.0 Crate root

```rust
pub mod asset_manager;
pub mod change_set;
pub mod cli;            // NEW at this milestone
pub mod config;
pub mod conflicts;
pub mod error;
pub mod format;
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
    ObjectStorage, Storage, StorageError, new_in_memory_storage,
    new_local_filesystem_storage, new_s3_storage,
};
pub use store::Store;
```

### 4.1 Errors — `icechunk::error` (inherited from milestone 04)

```rust
pub struct ICError<E> { pub kind: E, pub context: /* opaque */ }
impl<E> ICError<E> {
    pub fn new(kind: E) -> Self;
    pub fn kind(&self) -> &E;
}
```

Every fallible subsystem exposes a `…ErrorKind` enum and a
`type …Error = ICError<…ErrorKind>` alias. The drivers **match on `err.kind`**,
never on a message. The variants the drivers match are named where they are
used below.

### 4.2 `icechunk::format` (inherited)

```rust
#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Path(/* private */);
impl Path { pub fn root() -> Path; pub fn new(p: &str) -> Result<Path, PathError>; }
impl TryFrom<&str> for Path { type Error = PathError; }

pub type SnapshotId  = ObjectId<12, SnapshotTag>;
pub type ManifestId  = ObjectId<12, ManifestTag>;
pub type ChunkId     = ObjectId<12, ChunkTag>;
pub type NodeId      = ObjectId<8,  NodeTag>;
impl<const SIZE: usize, T: FileTypeTag> ObjectId<SIZE, T> { pub fn random() -> Self; }

#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct ChunkIndices(pub Vec<u32>);

pub enum ByteRange { Bounded(Range<u64>), From(u64), Last(u64), Until(u64) }
impl ByteRange { pub const ALL: Self; /* = From(0) */ }

pub enum IcechunkFormatErrorKind { /* non_exhaustive */ }
pub type IcechunkFormatError  = ICError<IcechunkFormatErrorKind>;
pub type IcechunkResult<T>    = Result<T, IcechunkFormatError>;
```

`ObjectId` and its four aliases are `Clone`, `PartialEq`, `Eq`, `PartialOrd`,
`Ord` and `Hash`, and implement `Debug`; section 9 excludes the *text* that
`Debug` produces, never the impl itself. Unchanged from milestone 04, whose
contract spelled the same annotation.

`Path::new` accepts only absolute, canonic paths, and `TryFrom<&str>` is the
same function: a path that does not start with `/` is an error, and so is one
that still contains a `.` or `..` component after normalisation. Unchanged
from milestone 04, and restated here because a driver observes it.

`ArrayShape::new` takes one `(array_len, chunk_len)` pair per dimension and
`valid_chunk_coord` answers whether a `ChunkIndices` lies inside the resulting
chunk grid — for a single dimension of `(4, 2)` the grid holds two chunks, so
index 1 is valid and index 2 is not. Also unchanged from milestone 04.

`ByteRange::Bounded(a..b)` is the **half-open** byte interval `[a, b)`: over a
five-byte object `Bounded(1..3)` reads the second and third bytes. `From(a)`
reads from byte `a` to the end of the object, and `ByteRange::ALL` is
`From(0)` — the whole object. `Last(n)` is the final `n` bytes. No group at
this milestone constructs `Until`, so nothing here fixes what it means.

`icechunk::format::manifest`:

```rust
pub struct VirtualChunkLocation(pub String);
impl VirtualChunkLocation {
    pub fn from_absolute_path(path: &str) -> Result<Self, VirtualReferenceError>;
}
pub struct SecondsSinceEpoch(pub u32);
pub enum Checksum { LastModified(SecondsSinceEpoch), ETag(ETag) }   // ETag wraps a String
pub struct VirtualChunkRef { pub location: VirtualChunkLocation, pub offset: u64,
                             pub length: u64, pub checksum: Option<Checksum> }
pub struct ChunkRef { pub id: ChunkId, pub offset: u64, pub length: u64 }
pub enum ChunkPayload { Inline(Bytes), Virtual(VirtualChunkRef), Ref(ChunkRef) }  // non_exhaustive
```

`VirtualChunkLocation::from_absolute_path` places **no restriction on the
scheme**: it parses the string as an absolute URL and accepts any scheme,
so `s3://bucket/object`, `gcs://bucket/object` and `file:///abs/path` are all
`Ok`. It performs no I/O and consults no container — whether a container
exists that can read the location is a separate question, asked only when the
reference is read (section 4.11). What it does reject is shape, not scheme: a
string that does not parse as a URL at all, a URL with no hierarchical path
segments, and a URL that carries no host under a scheme other than `file`, are
each an `Err`.

`icechunk::format::snapshot`:

```rust
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ArrayShape(/* private */);
impl ArrayShape {
    pub fn new<I: IntoIterator<Item = (u64, u64)>>(it: I) -> Option<Self>;  // (array_len, chunk_len)
    pub fn valid_chunk_coord(&self, coord: &ChunkIndices) -> bool;
}
pub enum DimensionName { NotSpecified, Name(String) }
impl From<Option<&str>> for DimensionName;   // None -> NotSpecified
impl From<&str> for DimensionName;           // ""   -> NotSpecified
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum NodeData { Array { shape: ArrayShape, dimension_names: Option<Vec<DimensionName>>,
                            manifests: Vec<ManifestRef> }, Group }
#[derive(Clone, Debug, Hash, PartialEq, Eq)]
pub enum NodeType { Group, Array }
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct NodeSnapshot { pub id: NodeId, pub path: Path, pub user_data: Bytes,
                          pub node_data: NodeData }
impl NodeSnapshot { pub fn node_type(&self) -> NodeType; }

pub type SnapshotProperties = std::collections::BTreeMap<String, serde_json::Value>;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ManifestFileInfo { pub id: ManifestId, pub size_bytes: u64, pub num_chunk_refs: u32 }

pub struct Snapshot { /* FlatBuffers-backed, milestone 04 */ }
impl Snapshot {
    pub fn id(&self) -> SnapshotId;
    pub fn parent_id(&self) -> Option<SnapshotId>;
    pub fn message(&self) -> String;
    pub fn metadata(&self) -> IcechunkResult<SnapshotProperties>;
    pub fn flushed_at(&self) -> IcechunkResult<DateTime<Utc>>;
    pub fn manifest_files(&self) -> impl Iterator<Item = ManifestFileInfo> + '_;
    pub fn manifest_info(&self, id: &ManifestId) -> Option<ManifestFileInfo>;
    pub fn get_node(&self, path: &Path) -> IcechunkResult<NodeSnapshot>;
    pub fn iter(&self) -> impl Iterator<Item = IcechunkResult<NodeSnapshot>> + '_;
    pub fn bytes(&self) -> &[u8];
    pub fn len(&self) -> usize;          // number of NODES, not bytes of `bytes()`
}
```

**`SnapshotInfo` — CHANGED at this milestone.** It gained `manifests`:

```rust
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SnapshotInfo {
    pub id: SnapshotId,
    pub parent_id: Option<SnapshotId>,
    pub flushed_at: DateTime<chrono::Utc>,
    pub message: String,
    pub metadata: SnapshotProperties,
    pub manifests: Vec<ManifestFileInfo>,   // NEW
}
impl SnapshotInfo { pub fn is_initial(&self) -> bool; }   // parent_id.is_none()
```

Required behaviour of `manifests`:

- a commit that writes **no chunk reference** — the initial snapshot, a commit
  that only adds groups, a commit that only creates an empty array — has an
  **empty** `manifests`;
- a commit that writes chunk references for one array reports **one**
  `ManifestFileInfo` whose `num_chunk_refs` is the number of chunk references
  that manifest holds and whose `size_bytes` is greater than zero;
- a later commit that does not touch chunks still reports the manifest the
  array's data lives in, with the **same** `ManifestId`.

`icechunk::format::transaction_log`:

```rust
pub struct TransactionLog { /* FlatBuffers-backed */ }
impl TransactionLog {
    pub fn new_groups(&self)     -> impl Iterator<Item = NodeId> + '_;
    pub fn new_arrays(&self)     -> impl Iterator<Item = NodeId> + '_;
    pub fn deleted_groups(&self) -> impl Iterator<Item = NodeId> + '_;
    pub fn deleted_arrays(&self) -> impl Iterator<Item = NodeId> + '_;
    pub fn updated_groups(&self) -> impl Iterator<Item = NodeId> + '_;
    pub fn updated_arrays(&self) -> impl Iterator<Item = NodeId> + '_;
    pub fn group_created(&self, id: &NodeId) -> bool;
    pub fn array_created(&self, id: &NodeId) -> bool;
    pub fn chunks_updated(&self, id: &NodeId) -> bool;
}
#[derive(Clone, Debug, PartialEq)]
pub struct Diff { pub new_groups: BTreeSet<Path>, pub new_arrays: BTreeSet<Path>,
                  pub deleted_groups: BTreeSet<Path>, pub deleted_arrays: BTreeSet<Path>,
                  pub updated_groups: BTreeSet<Path>, pub updated_arrays: BTreeSet<Path>,
                  pub updated_chunks: BTreeMap<Path, BTreeSet<ChunkIndices>> }
```

Each of the six `TransactionLog` iterators reports the nodes of one class that
**the commit this log belongs to** changed, and nothing from any other commit:
`new_groups` / `new_arrays` yield the `NodeId`s the commit created,
`deleted_groups` / `deleted_arrays` those it removed, and `updated_groups` /
`updated_arrays` those whose user data it modified. A commit that changed
nothing of a class yields an **empty** iterator for that class, not an error.
`group_created(id)` and `array_created(id)` answer whether that one `NodeId`
is among the groups, respectively arrays, the commit created, and
`chunks_updated(id)` whether the commit changed that node's chunk references —
so it is `false` for a group, which has none. A transaction log's on-disk
fields are recorded in `icechunk-spec/flatbuffers/transaction_log.fbs`, which
carries these six lists plus the per-array chunk-reference changes;
`PROFILE.md`'s D3 notes that two of that file's comments are swapped and that
the **field names** are the correct ones.

### 4.3 `icechunk::repository::Repository`

```rust
#[non_exhaustive]
pub enum VersionInfo {
    SnapshotId(SnapshotId),
    TagRef(String),
    BranchTipRef(String),
    AsOf { branch: String, at: DateTime<Utc> },
}

#[non_exhaustive]
pub enum RepositoryErrorKind {
    SnapshotNotFound { id: SnapshotId },
    InvalidAsOfSpec { branch: String, at: DateTime<Utc> },   // NEW
    ReadonlyStorage(String),                                 // NEW
    InvalidSnapshotId(String),
    Tag(String),
    ParentDirectoryNotClean,
    RepositoryDoesntExist,
    ConfigWasUpdated,
    Conflict { expected_parent: Option<SnapshotId>, actual_parent: Option<SnapshotId> },
    CannotDeleteMain,
    /* plus transparent wrappers over the storage, format and ref kinds */
}
pub type RepositoryError  = ICError<RepositoryErrorKind>;
pub type RepositoryResult<T> = Result<T, RepositoryError>;

impl Repository {
    pub async fn create(config: Option<RepositoryConfig>,
                        storage: Arc<dyn Storage + Send + Sync>,
                        virtual_chunk_credentials: HashMap<ContainerName, Credentials>)
        -> RepositoryResult<Self>;
    pub async fn open(/* same three */) -> RepositoryResult<Self>;
    pub async fn open_or_create(/* same three */) -> RepositoryResult<Self>;
    pub async fn exists(storage: &(dyn Storage + Send + Sync)) -> RepositoryResult<bool>;

    pub fn config(&self) -> &RepositoryConfig;
    pub fn storage_settings(&self) -> &storage::Settings;
    pub fn storage(&self) -> &Arc<dyn Storage + Send + Sync>;
    pub fn asset_manager(&self) -> &Arc<AssetManager>;

    pub fn from_bytes(bytes: Vec<u8>) -> RepositoryResult<Self>;   // NEW
    pub fn as_bytes(&self) -> RepositoryResult<Vec<u8>>;           // NEW

    pub async fn list_branches(&self) -> RepositoryResult<BTreeSet<String>>;
    pub async fn list_tags(&self)     -> RepositoryResult<BTreeSet<String>>;
    pub async fn lookup_branch(&self, branch: &str) -> RepositoryResult<SnapshotId>;
    pub async fn lookup_tag(&self, tag: &str)       -> RepositoryResult<SnapshotId>;
    pub async fn create_branch(&self, name: &str, snapshot_id: &SnapshotId) -> RepositoryResult<()>;
    pub async fn create_tag(&self, name: &str, snapshot_id: &SnapshotId)    -> RepositoryResult<()>;
    pub async fn reset_branch(&self, branch: &str, snapshot_id: &SnapshotId) -> RepositoryResult<()>;
    pub async fn delete_branch(&self, branch: &str) -> RepositoryResult<()>;
    pub async fn delete_tag(&self, tag: &str)       -> RepositoryResult<()>;

    pub async fn resolve_version(&self, version: &VersionInfo) -> RepositoryResult<SnapshotId>;
    pub async fn ancestry<'a>(&'a self, version: &VersionInfo)
        -> RepositoryResult<impl Stream<Item = RepositoryResult<SnapshotInfo>> + 'a>;
    pub async fn snapshot_ancestry(&self, snapshot_id: &SnapshotId)
        -> RepositoryResult<impl Stream<Item = RepositoryResult<SnapshotInfo>> + '_>;
    pub async fn diff(&self, from: &VersionInfo, to: &VersionInfo) -> SessionResult<Diff>;

    pub async fn readonly_session(&self, version: &VersionInfo) -> RepositoryResult<Session>;
    pub async fn writable_session(&self, branch: &str)          -> RepositoryResult<Session>;

    // NEW at this milestone
    pub async fn lookup_snapshot(&self, snapshot_id: &SnapshotId) -> RepositoryResult<SnapshotInfo>;
    pub fn set_default_commit_metadata(&mut self, metadata: SnapshotProperties);
    pub fn default_commit_metadata(&self) -> &SnapshotProperties;
}
```

`Repository::create` leaves the repository holding exactly one snapshot — the
initial one — and that snapshot holds **no nodes**, not even a root group. The
first commit on `main` therefore creates the root itself: `add_group` at
`Path::root()` on a session over a freshly created repository succeeds rather
than colliding with a node that is already there, and that is how nearly every
history the drivers build begins. (`spec.md`'s `### Initialize New Repository`
calls what `create` writes a "[possibly empty]" snapshot; at this milestone it
is empty.)

`config()` reports the repository's **configuration**, never a resolved view
of it. On `create` it is exactly the `RepositoryConfig` the caller supplied —
a field the caller left `None` is still `None` here — and on `open` it is the
configuration persisted in the repository with the caller's non-`None` values
applied over it. In neither case is the backend's own `default_settings()`
folded in, so `config().storage()` is `None` for a repository created with no
`storage` of its own. What the repository is actually **using** for storage is
`storage_settings()` (section 4.8), which falls back to the backend's
`default_settings()` exactly when `config().storage()` is `None`.

`resolve_version` answers the `VersionInfo` it is handed with the snapshot that
version names: `SnapshotId(i)` resolves to `i`, `TagRef(t)` to the snapshot the
tag was created at, `BranchTipRef(b)` to that branch's current tip, and a
branch or tag name that does not exist is an `Err`. `AsOf` is out of scope
(section 9).

#### 4.3.1 `lookup_snapshot` — required behaviour

`lookup_snapshot(id)` answers from **storage**, not from the reference graph:

| situation | result |
| --- | --- |
| `id` names a snapshot object that exists | `Ok(SnapshotInfo)` describing it, with `id`, `parent_id`, `message`, `metadata` and `manifests` |
| `id` names a snapshot no reference reaches, but whose object is still stored | `Ok(SnapshotInfo)` — being unreachable is not being absent |
| `id` was never written, or its object has been deleted by garbage collection | `Err` |

`ancestry` yields `SnapshotInfo` in **decreasing `flushed_at` order**, newest
first, ending with the repository's initial snapshot (the one whose
`parent_id` is `None`). Each commit appends exactly one snapshot to a branch's
ancestry.

`snapshot_ancestry(id)` is that same walk starting from a snapshot id rather
than from a reference: it yields the snapshot `id` names first, then that
snapshot's ancestors in the same decreasing-`flushed_at` order, ending at the
initial snapshot. Started part-way back through a history it yields that
snapshot and its ancestors only, never anything newer. `ancestry(version)` is
`resolve_version(version)` followed by exactly this walk.

#### 4.3.2 Reference creation validates its snapshot — NEW

`create_branch` and `create_tag` must **refuse** a `snapshot_id` that does not
exist in storage, returning `Err` and writing **no reference**: after a refused
call the name must not be listed by `list_branches` / `list_tags`, must not
resolve through `lookup_branch` / `lookup_tag`, and must still be available for
a later, valid creation. `reset_branch` validates its target the same way.

`delete_branch("main")` fails with `RepositoryErrorKind::CannotDeleteMain`;
`"main"` is the default branch name.

#### 4.3.3 Repository-level commit metadata — NEW

A `Repository` carries a `SnapshotProperties` map of **default commit
metadata**, empty on a freshly created or opened repository. `Session::commit`
merges it with the caller's properties, **key by key, with the caller
winning**:

| `commit(msg, properties)` | resulting `SnapshotInfo::metadata` |
| --- | --- |
| defaults empty, `properties = None` | empty |
| defaults set, `properties = None` | exactly the defaults |
| defaults set, `properties = Some(p)` | the defaults, then every entry of `p` written over it |

Commits made **before** the defaults were set are unaffected; every commit made
after, on any branch, carries them.

### 4.4 `icechunk::session::Session`

```rust
pub enum SessionErrorKind {
    ReadOnlySession,
    SnapshotNotFound { id: SnapshotId },
    NodeNotFound { path: Path, message: String },
    NotAnArray { node: NodeSnapshot, message: String },
    NotAGroup  { node: NodeSnapshot, message: String },
    AlreadyExists { node: NodeSnapshot, message: String },
    NoChangesToCommit,
    Conflict { expected_parent: Option<SnapshotId>, actual_parent: Option<SnapshotId> },
    RebaseFailed { snapshot: SnapshotId, conflicts: Vec<Conflict> },
    InvalidIndex { coords: ChunkIndices, path: Path },
    BadSnapshotChainForDiff,
    /* plus transparent wrappers */
}
pub type SessionError  = ICError<SessionErrorKind>;
pub type SessionResult<T> = Result<T, SessionError>;

impl Session {
    pub fn branch(&self) -> Option<&str>;
    pub fn read_only(&self) -> bool;
    pub fn snapshot_id(&self) -> &SnapshotId;
    pub fn has_uncommitted_changes(&self) -> bool;
    pub fn changes(&self) -> &ChangeSet;
    pub fn config(&self) -> &RepositoryConfig;
    pub fn matching_container(&self, loc: &VirtualChunkLocation) -> Option<&VirtualChunkContainer>;
    pub async fn status(&self) -> SessionResult<Diff>;

    pub async fn add_group(&mut self, path: Path, definition: Bytes) -> SessionResult<()>;
    pub async fn add_array(&mut self, path: Path, shape: ArrayShape,
                           dimension_names: Option<Vec<DimensionName>>,
                           user_data: Bytes) -> SessionResult<()>;
    pub async fn update_array(&mut self, path: &Path, shape: ArrayShape,
                              dimension_names: Option<Vec<DimensionName>>,
                              user_data: Bytes) -> SessionResult<()>;
    pub async fn update_group(&mut self, path: &Path, definition: Bytes) -> SessionResult<()>;
    pub async fn delete_array(&mut self, path: Path) -> SessionResult<()>;
    pub async fn delete_group(&mut self, path: Path) -> SessionResult<()>;
    pub async fn get_node(&self, path: &Path)  -> SessionResult<NodeSnapshot>;
    pub async fn get_array(&self, path: &Path) -> SessionResult<NodeSnapshot>;
    pub async fn get_group(&self, path: &Path) -> SessionResult<NodeSnapshot>;
    pub async fn list_nodes(&self) -> SessionResult<impl Iterator<Item = SessionResult<NodeSnapshot>>>;

    pub async fn set_chunk_ref(&mut self, path: Path, coord: ChunkIndices,
                               data: Option<ChunkPayload>) -> SessionResult<()>;
    pub async fn get_chunk_ref(&self, path: &Path, coords: &ChunkIndices)
        -> SessionResult<Option<ChunkPayload>>;
    pub async fn get_chunk_reader(&self, path: &Path, coords: &ChunkIndices,
                                  byte_range: &ByteRange)
        -> SessionResult<Option<Pin<Box<dyn Future<Output = SessionResult<Bytes>> + Send>>>>;
    pub fn get_chunk_writer(&self)
        -> impl FnOnce(Bytes) -> Pin<Box<dyn Future<Output = SessionResult<ChunkPayload>> + Send>>;

    pub async fn commit(&mut self, message: &str, properties: Option<SnapshotProperties>)
        -> SessionResult<SnapshotId>;
    pub async fn rebase(&mut self, solver: &dyn ConflictSolver) -> SessionResult<()>;

    pub fn as_bytes(&self) -> SessionResult<Vec<u8>>;
    pub fn from_bytes(bytes: Vec<u8>) -> SessionResult<Self>;
    pub async fn merge(&mut self, changes: ChangeSet) -> SessionResult<()>;
    pub fn discard_changes(&mut self) -> ChangeSet;    // NEW at this milestone
}
impl From<Session> for ChangeSet { /* takes the session's change set */ }
```

Inherited behaviour a driver observes, unchanged from milestone 04 and
restated here: `status()` is the `Diff` of **this session's own uncommitted
changes** - not a comparison against another snapshot, which is
`Repository::diff` and is out of scope (section 9) - and it is empty for a fresh
session.

```rust
pub async fn get_chunk(
    reader: Option<Pin<Box<dyn Future<Output = SessionResult<Bytes>> + Send>>>,
) -> SessionResult<Option<Bytes>>;
```

#### 4.4.1 The distributed-write reduction — required behaviour

A "cooperative" distributed write is a *reduction*: many writers each produce a
change set, and one coordinator folds them all into a single session and
commits **once**.

```text
writable_session ──▶ write chunks ──▶ Session::as_bytes ──┐
                                                          ├─▶ Session::from_bytes
coordinator writable_session ◀── merge(ChangeSet) ◀───────┘  ──▶ Into<ChangeSet>
                   └──▶ commit()   →  ONE new snapshot
```

Required:

- `Session::as_bytes` / `Session::from_bytes` round-trip a writable session
  including its uncommitted changes, its branch name and its base snapshot id;
- `impl From<Session> for ChangeSet` lets a revived session be merged;
- `merge` on a **read-only** session fails with
  `SessionErrorKind::ReadOnlySession`;
- after folding *n* writers' change sets into one session and committing, the
  branch gains exactly **one** snapshot, and every chunk any writer wrote is
  readable from it;
- `discard_changes` removes the change set from the session and returns it:
  afterwards `has_uncommitted_changes()` is `false`, and merging the returned
  change set into another writable session restores the work.

### 4.5 `icechunk::change_set::ChangeSet` and `icechunk::conflicts` (inherited)

```rust
pub struct ChangeSet { /* … */ }
impl ChangeSet { pub fn is_empty(&self) -> bool; }

// icechunk::conflicts
#[derive(Debug, PartialEq, Eq)]
pub enum Conflict {
    NewNodeConflictsWithExistingNode(Path),
    NewNodeInInvalidGroup(Path),
    ZarrMetadataDoubleUpdate(Path),
    ZarrMetadataUpdateOfDeletedArray(Path),
    ZarrMetadataUpdateOfDeletedGroup(Path),
    ChunkDoubleUpdate { path: Path, node_id: NodeId, chunk_coordinates: HashSet<ChunkIndices> },
    ChunksUpdatedInDeletedArray { path: Path, node_id: NodeId },
    ChunksUpdatedInUpdatedArray { path: Path, node_id: NodeId },
    DeleteOfUpdatedArray { path: Path, node_id: NodeId },
    DeleteOfUpdatedGroup { path: Path, node_id: NodeId },
}
pub enum ConflictResolution { Patched(ChangeSet), Unsolvable { reason: Vec<Conflict>, unmodified: ChangeSet } }
#[async_trait] pub trait ConflictSolver { /* … */ }

// icechunk::conflicts::detector
pub struct ConflictDetector;
// icechunk::conflicts::basic_solver
pub enum VersionSelection { Fail, UseOurs, UseTheirs }
pub struct BasicConflictSolver {
    pub on_chunk_conflict: VersionSelection,
    pub fail_on_delete_of_updated_array: bool,
    pub fail_on_delete_of_updated_group: bool,
}
impl Default for BasicConflictSolver { /* UseOurs, false, false */ }
```

Two writable sessions built from the same branch tip: the first `commit`
succeeds, the second fails with `SessionErrorKind::Conflict { .. }`. After
`rebase` with a solver that can resolve the differences, the second `commit`
succeeds and both writers' changes are present. A rebase the solver cannot
resolve fails with `SessionErrorKind::RebaseFailed { conflicts, .. }` whose
`conflicts` names the classes above.

### 4.6 `icechunk::store::Store` (inherited, the Zarr V3 key/value view)

```rust
pub enum ListDirItem { Key(String), Prefix(String) }
pub enum StoreErrorKind { NotFound(KeyNotFoundError), ReadOnly, BadMetadata(..), /* … */ }
pub type StoreError  = ICError<StoreErrorKind>;
pub type StoreResult<T> = Result<T, StoreError>;

impl Store {
    pub async fn from_session(session: Arc<tokio::sync::RwLock<Session>>) -> Self;
    pub fn from_session_and_config(session: Arc<RwLock<Session>>, concurrency: u16) -> Self;
    pub fn session(&self) -> Arc<RwLock<Session>>;
    pub async fn read_only(&self) -> bool;
    pub async fn is_empty(&self, prefix: &str) -> StoreResult<bool>;
    pub async fn clear(&self) -> StoreResult<()>;
    pub async fn get(&self, key: &str, byte_range: &ByteRange) -> StoreResult<Bytes>;
    pub async fn set(&self, key: &str, value: Bytes) -> StoreResult<()>;
    pub async fn set_if_not_exists(&self, key: &str, value: Bytes) -> StoreResult<()>;
    pub async fn set_virtual_ref(&self, key: &str, reference: VirtualChunkRef,
                                 validate_container: bool) -> StoreResult<()>;
    pub async fn delete(&self, key: &str) -> StoreResult<()>;
    pub async fn delete_dir(&self, prefix: &str) -> StoreResult<()>;
    pub async fn exists(&self, key: &str) -> StoreResult<bool>;
    pub async fn list(&self) -> StoreResult<impl Stream<Item = StoreResult<String>> + Send>;
    pub async fn list_prefix(&self, prefix: &str) -> StoreResult<impl Stream<Item = StoreResult<String>> + Send>;
    pub async fn list_dir(&self, prefix: &str) -> StoreResult<impl Stream<Item = StoreResult<String>> + Send>;
    pub async fn list_dir_items(&self, prefix: &str) -> StoreResult<impl Stream<Item = StoreResult<ListDirItem>> + Send>;
    pub async fn getsize(&self, key: &str) -> StoreResult<u64>;
    pub async fn getsize_prefix(&self, prefix: &str) -> StoreResult<u64>;
}
```

Keys are Zarr V3 keys: `zarr.json` for the root group, `<path>/zarr.json` for a
node, and `<path>/c/<i>/<j>/…` for a chunk under the default chunk-key
encoding. Setting a `zarr.json` whose document is a Zarr V3 **array** metadata
creates an array; a **group** document creates a group. `list()` yields every
metadata key and every written chunk key.

Inherited behaviour a driver observes, unchanged from milestone 04 and
restated here because this document is the only place this milestone states
it:

- `getsize(key)` reports **payload** bytes and never the store's own overhead:
  it is the byte length of what `get(key, &ByteRange::ALL)` would return — for
  a metadata key the stored Zarr document exactly as the caller supplied it,
  for a chunk key the chunk's data.
- `list_dir_items(prefix)` reports the *immediate* contents of `prefix` and
  distinguishes the two kinds: the metadata stored at `prefix` itself is a
  `ListDirItem::Key`, and each node directly below it is a
  `ListDirItem::Prefix`. So the root of a store holding `zarr.json` and
  `array/zarr.json` yields `Key("zarr.json")` and a prefix naming `array`;
  whether a prefix carries a trailing separator is not asserted.
- A `get` at a key nothing was written at fails with
  `StoreErrorKind::NotFound(_)`; it is never an empty value. Every write on a
  store over a read-only session fails with `StoreErrorKind::ReadOnly`.

### 4.7 `icechunk::config` — repository and object-store configuration

```rust
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct S3Options {
    pub region: Option<String>,
    pub endpoint_url: Option<String>,
    pub anonymous: bool,
    pub allow_http: bool,
    #[serde(default)]              // defaults to false when absent
    pub force_path_style: bool,    // NEW at this milestone
}
impl fmt::Display for S3Options { /* NEW */ }

pub enum ObjectStoreConfig { InMemory, LocalFileSystem(PathBuf), S3Compatible(S3Options),
                             S3(S3Options), Gcs(HashMap<String, String>),
                             Azure(HashMap<String, String>), Tigris(S3Options) }

pub struct S3StaticCredentials { pub access_key_id: String, pub secret_access_key: String,
                                 pub session_token: Option<String>,
                                 pub expires_after: Option<DateTime<Utc>> }
pub enum S3Credentials { FromEnv, Anonymous, Static(S3StaticCredentials), Refreshable(..) }
pub enum GcsCredentials { FromEnv, Static(GcsStaticCredentials), Refreshable(..), Anonymous }  // Anonymous is NEW
pub enum AzureCredentials { FromEnv, Static(AzureStaticCredentials) }
pub enum Credentials { S3(S3Credentials), Gcs(GcsCredentials), Azure(AzureCredentials) }

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq, Default)]
pub struct RepositoryConfig {
    pub inline_chunk_threshold_bytes: Option<u16>,
    pub get_partial_values_concurrency: Option<u16>,
    pub compression: Option<CompressionConfig>,
    pub caching: Option<CachingConfig>,
    pub storage: Option<storage::Settings>,
    pub virtual_chunk_containers: Option<HashMap<ContainerName, VirtualChunkContainer>>,
    pub manifest: Option<ManifestConfig>,
}
impl RepositoryConfig {
    pub fn inline_chunk_threshold_bytes(&self) -> u16;      // default 512
    pub fn get_partial_values_concurrency(&self) -> u16;    // default 10
    pub fn compression(&self) -> &CompressionConfig;
    pub fn caching(&self) -> &CachingConfig;
    pub fn storage(&self) -> Option<&storage::Settings>;
    pub fn manifest(&self) -> &ManifestConfig;
    pub fn merge(&self, other: Self) -> Self;
    pub fn set_virtual_chunk_container(&mut self, cont: VirtualChunkContainer);
    pub fn get_virtual_chunk_container(&self, name: &str) -> Option<&VirtualChunkContainer>;
    pub fn virtual_chunk_containers(&self) -> impl Iterator<Item = &VirtualChunkContainer>;
    pub fn clear_virtual_chunk_containers(&mut self);
}
```

`RepositoryConfig::merge(other)` follows the same rule as
`storage::Settings::merge` (section 4.8): for each **scalar** field the result
takes **`other`'s** value where `other` sets one and keeps its own otherwise.
(Where both sides set one of the nested configurations, they are merged by the
same rule rather than replaced wholesale; only the scalar behaviour is
asserted.) Each accessor supplies a default where its field is `None`, so
`compression()`, `caching()` and `manifest()` answer on an all-default
configuration.

The credential enums above are **serialised**, and their wire form is fixed by
section 5.5: each is *internally* tagged (`s3_credential_type`,
`gcs_credential_type`, `az_credential_type`) with `snake_case` values. That
encoding is what the command line's configuration file uses, so it binds these
`icechunk::config` types even though it is specified in the command-line
section.

`RepositoryConfig` gained **no new field** at this milestone. The new
configuration reaches a repository through `RepositoryConfig::storage`
(section 4.8).

A chunk written through `Session::get_chunk_writer` is stored **inline inside
the manifest** when its length is at most `inline_chunk_threshold_bytes()`, and
as its **own object** otherwise — **one object per chunk**, so the
`ChunkPayload::Ref(ChunkRef { .. })` the writer hands back records
`offset = 0` and a `length` equal to the chunk's own byte length.
`spec.md`'s "Chunk Files" allowance for packing several contiguous chunks of
one array into a single chunk file is **not** taken at this milestone.
`inline_chunk_threshold_bytes: Some(0)`
therefore makes every chunk a separate, individually collectable object, and
the default of 512 inlines every small chunk.

### 4.8 `icechunk::storage` — the backend trait and its settings

```rust
pub trait Storage: fmt::Debug + fmt::Display + Sync + Send {   // Display is NEW
    fn default_settings(&self) -> Settings { Default::default() }
    fn can_write(&self) -> bool;                                // NEW
    /* object read/write/list/delete methods; the trait is sealed */
}

#[derive(Debug, PartialEq, Eq, Serialize, Deserialize, Clone, Default)]
pub struct Settings {
    pub concurrency: Option<ConcurrencySettings>,
    pub unsafe_use_conditional_update: Option<bool>,   // NEW
    pub unsafe_use_conditional_create: Option<bool>,   // NEW
    pub unsafe_use_metadata: Option<bool>,             // NEW
}
impl Settings {
    pub fn concurrency(&self) -> &ConcurrencySettings;
    pub fn unsafe_use_conditional_create(&self) -> bool;   // unwrap_or(true)
    pub fn unsafe_use_conditional_update(&self) -> bool;   // unwrap_or(true)
    pub fn unsafe_use_metadata(&self) -> bool;             // unwrap_or(true)
    pub fn merge(&self, other: Self) -> Self;
}

#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct DeleteObjectsResult { pub deleted_objects: u64, pub deleted_bytes: u64 }

pub struct ListInfo<Id> { pub id: Id, pub created_at: DateTime<Utc>, pub size_bytes: u64 }  // size_bytes is NEW

pub enum StorageErrorKind { Other(String), R2ConfigurationError(String), /* … */ }   // R2… is NEW
pub type StorageError  = ICError<StorageErrorKind>;
pub type StorageResult<T> = Result<T, StorageError>;
```

Required behaviour of the three `unsafe_use_*` switches:

- each is a **tri-state** `Option<bool>`; the accessor reads `true` when the
  option is `None`, so `Settings::default()` has every switch **on** while
  every field is `None`;
- `Settings::merge(other)` takes **`other`'s** value for each of the three when
  `other` sets it, and keeps its own otherwise;
- `RepositoryConfig::storage` overrides the backend's `default_settings()` for
  the repository built with it, and `Repository::storage_settings()` reports
  what the repository is using;
- a backend declares the switches it cannot honour in its `default_settings()`.
  The **local-filesystem** backend has neither conditional updates nor object
  metadata, so its `default_settings()` reports
  `unsafe_use_conditional_update() == false` and `unsafe_use_metadata() ==
  false`. The **in-memory** backend supports both, so its defaults leave them
  on.
- conditional **create** is the one conditional operation a local filesystem
  can honour, and no backend at this milestone turns it off. The
  local-filesystem backend therefore leaves `unsafe_use_conditional_create`
  unset, so its `default_settings().unsafe_use_conditional_create()` reports
  **`true`** — and so does `Repository::storage_settings()` on a repository
  built over that backend with no `RepositoryConfig::storage` of its own,
  because an unconfigured repository reports what the backend declares rather
  than a blanket `Settings::default()`. Read the bullet above as naming the
  two switches the local-filesystem backend turns off, not as covering
  conditional operations in general.

Backend constructors:

```rust
pub async fn new_in_memory_storage() -> StorageResult<Arc<dyn Storage>>;
pub async fn new_local_filesystem_storage(path: &std::path::Path) -> StorageResult<Arc<dyn Storage>>;
pub fn new_s3_storage(config: S3Options, bucket: String, prefix: Option<String>,
                      credentials: Option<S3Credentials>) -> StorageResult<Arc<dyn Storage>>;
pub async fn new_s3_object_store_storage(/* same four */) -> StorageResult<Arc<dyn Storage>>;
pub async fn new_gcs_storage(bucket: String, prefix: Option<String>,
                             credentials: Option<GcsCredentials>,
                             config: Option<HashMap<String, String>>) -> StorageResult<Arc<dyn Storage>>;
pub async fn new_azure_blob_storage(account: String, container: String, prefix: Option<String>,
                                    credentials: Option<AzureCredentials>,
                                    config: Option<HashMap<String, String>>) -> StorageResult<Arc<dyn Storage>>;

// NEW at this milestone
pub fn new_r2_storage(config: S3Options, bucket: Option<String>, prefix: Option<String>,
                      account_id: Option<String>, credentials: Option<S3Credentials>)
    -> StorageResult<Arc<dyn Storage>>;
// CHANGED at this milestone: a fifth parameter
pub fn new_tigris_storage(config: S3Options, bucket: String, prefix: Option<String>,
                          credentials: Option<S3Credentials>, use_weak_consistency: bool)
    -> StorageResult<Arc<dyn Storage>>;
```

Required behaviour of the cloud constructors. **None of them may perform I/O**:
each returns a `Storage` whose client is built lazily, so constructing one with
made-up credentials succeeds or fails purely on its configuration.

| constructor | required outcome |
| --- | --- |
| `new_s3_storage` / `new_s3_object_store_storage` with an `endpoint_url` containing `fly.storage.tigris.dev` | `Err` — Tigris has its own constructor |
| `new_s3_storage` / `new_s3_object_store_storage` with any other configuration | `Ok`, and `can_write()` is `true` |
| `new_tigris_storage(.., use_weak_consistency = false)` with no `region` | `Err` — the consistent mode needs a region |
| `new_tigris_storage(.., use_weak_consistency = false)` with a `region` | `Ok`, and `can_write()` is `true` |
| `new_tigris_storage(.., use_weak_consistency = true)` | `Ok` with or without a region, and `can_write()` is **`false`** — the eventually consistent mode is read-only |
| `new_r2_storage` with neither `endpoint_url` nor `account_id` | `Err`, kind `StorageErrorKind::R2ConfigurationError` |
| `new_r2_storage` with neither `bucket` nor `prefix` | `Err`, kind `StorageErrorKind::R2ConfigurationError` |
| `new_r2_storage` with a bucket and an `account_id` | `Ok`, and `can_write()` is `true` |
| `new_r2_storage` with a bucket and an `endpoint_url` but **no** `account_id` | `Ok`, and `can_write()` is `true` — the endpoint url and the account id are alternatives, and either one alone satisfies the rule the first row rejects |

Because `Storage` now requires `Display`, every backend renders a description
of itself. That rendering **must not contain a credential**: neither an access
key id nor a secret access key may appear in it. Its wording is not otherwise
constrained (section 9).

### 4.9 `icechunk::asset_manager::AssetManager` (inherited, extended)

```rust
impl AssetManager {
    pub fn new_no_cache(storage: Arc<dyn Storage + Send + Sync>,
                        storage_settings: storage::Settings,
                        compression_level: u8) -> Self;
    pub fn new_with_config(storage: Arc<dyn Storage + Send + Sync>,
                           storage_settings: storage::Settings,
                           config: &CachingConfig, compression_level: u8) -> Self;

    pub async fn fetch_snapshot(&self, id: &SnapshotId) -> RepositoryResult<Arc<Snapshot>>;
    pub async fn fetch_snapshot_info(&self, id: &SnapshotId) -> RepositoryResult<SnapshotInfo>;  // NEW
    pub async fn fetch_manifest(&self, id: &ManifestId, size_bytes: u64) -> RepositoryResult<Arc<Manifest>>;
    pub async fn fetch_transaction_log(&self, id: &SnapshotId) -> RepositoryResult<Arc<TransactionLog>>;
    pub async fn snapshot_ancestry(self: Arc<Self>, id: &SnapshotId)
        -> RepositoryResult<impl Stream<Item = RepositoryResult<SnapshotInfo>>>;

    // NEW at this milestone
    pub fn remove_cached_snapshot(&self, id: &SnapshotId);
    pub fn remove_cached_manifest(&self, id: &ManifestId);
    pub fn remove_cached_tx_log(&self, id: &SnapshotId);
    pub fn clear_chunk_cache(&self);
}
```

### 4.10 `icechunk::ops` — roots, statistics, expiration and collection

```rust
// icechunk::ops  (lifted out of ops::gc and made public at this milestone)
pub async fn all_roots<'a>(storage: &'a (dyn Storage + Send + Sync),
                           storage_settings: &'a storage::Settings,
                           extra_roots: &'a HashSet<SnapshotId>)
    -> RefResult<impl Stream<Item = RefResult<SnapshotId>> + 'a>;
pub async fn pointed_snapshots<'a>(storage: &'a (dyn Storage + Send + Sync),
                                   storage_settings: &'a storage::Settings,
                                   asset_manager: Arc<AssetManager>,
                                   extra_roots: &'a HashSet<SnapshotId>)
    -> RepositoryResult<impl Stream<Item = RepositoryResult<SnapshotId>> + 'a>;

// icechunk::ops::stats  (NEW module)
pub async fn repo_chunks_storage(storage: &(dyn Storage + Send + Sync),
                                 storage_settings: &storage::Settings,
                                 asset_manager: Arc<AssetManager>) -> RepositoryResult<u64>;
```

`all_roots` yields one `SnapshotId` per reference in the repository — the
snapshot that reference points at — followed by every id in `extra_roots`,
which it does not resolve. `pointed_snapshots` yields, for each of those roots,
**the root itself and every snapshot in its ancestry**. Both may yield the same
id more than once (two references can point at one snapshot, and two roots
share their ancestors) and neither fixes an order, so a caller reads either
stream as a **set**. Together they are the reachability the rest of this module
rests on: a snapshot `pointed_snapshots` does not yield is unreachable, and is
exactly what `garbage_collect` may delete and what `repo_chunks_storage` does
not count.

`repo_chunks_storage` returns the **total size in bytes of every committed
chunk object** the repository's references can reach, counting each distinct
chunk once however many manifests refer to it. It does **not** count:

- uncommitted chunks — a session that has written but not committed adds
  nothing;
- **inline** chunks, which live inside the manifest rather than as objects.
  Under the default `inline_chunk_threshold_bytes` of 512, small chunks are
  inline and this function legitimately returns `0`;
- **virtual** chunks, whose bytes live outside the repository.

A committed native chunk object holds the bytes **exactly as they were handed
to `Session::get_chunk_writer`**: the repository adds no framing, no padding
and no compression of its own, so the `ChunkRef::length` recorded for the
chunk is the caller's payload length and `repo_chunks_storage` over *n*
distinct reachable chunks of *b* bytes each is exactly `n · b`.
`RepositoryConfig::compression` governs the **metadata** files — snapshots,
manifests and transaction logs — and never a chunk object. (When `spec.md`'s
"Chunk Files" calls a chunk file compressed it means the Zarr codec pipeline's
output, which is already in the bytes the caller supplies.)

```rust
// icechunk::ops::gc
pub enum Action { Keep, DeleteIfCreatedBefore(DateTime<Utc>) }

pub struct GCConfig { /* private fields */ }
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

#[derive(Debug, PartialEq, Eq, Default)]
pub struct GCSummary {
    pub bytes_deleted: u64,             // NEW field
    pub chunks_deleted: u64,            // was usize
    pub manifests_deleted: u64,         // was usize
    pub snapshots_deleted: u64,         // was usize
    pub attributes_deleted: u64,        // was usize
    pub transaction_logs_deleted: u64,  // was usize
}

pub enum GCError { /* Ref, Storage, Repository, FormatError */ }
pub type GCResult<A> = Result<A, GCError>;

pub async fn garbage_collect(storage: &(dyn Storage + Send + Sync),
                             storage_settings: &storage::Settings,
                             asset_manager: Arc<AssetManager>,
                             config: &GCConfig) -> GCResult<GCSummary>;

#[derive(Debug, PartialEq, Eq, Clone)]
pub enum ExpireRefResult {
    NothingToDo { ref_is_expired: bool },                 // was a unit variant
    Done { released_snapshots: HashSet<SnapshotId>,
           edited_snapshot: SnapshotId,
           ref_is_expired: bool },                        // ref_is_expired is NEW
    // the `RefIsExpired` variant of the previous milestone is REMOVED
}

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum ExpiredRefAction { Delete, Ignore }

#[derive(Debug, PartialEq, Eq, Clone, Default)]
pub struct ExpireResult {
    pub released_snapshots: HashSet<SnapshotId>,
    pub edited_snapshots: HashSet<SnapshotId>,
    pub deleted_refs: HashSet<Ref>,
}

pub async fn expire_ref(storage: &(dyn Storage + Send + Sync),
                        storage_settings: &storage::Settings,
                        asset_manager: Arc<AssetManager>,
                        reference: &Ref,
                        older_than: DateTime<Utc>) -> GCResult<ExpireRefResult>;

pub async fn expire(storage: &(dyn Storage + Send + Sync),
                    storage_settings: &storage::Settings,
                    asset_manager: Arc<AssetManager>,
                    older_than: DateTime<Utc>,
                    expired_branches: ExpiredRefAction,   // 5th parameter
                    expired_tags: ExpiredRefAction)       // 6th parameter
    -> GCResult<ExpireResult>;
```

#### 4.10.1 Garbage-collection accounting — required behaviour

- Objects a reference can reach are **never** deleted. `GCConfig::clean_all`
  over a repository with nothing dangling returns `GCSummary::default()` — every
  counter zero — and leaves every chunk readable.
- A `GCConfig` in which every class is `Action::Keep` deletes nothing and
  returns `GCSummary::default()`, and its `deletes_*` predicates all report
  `false`.
- After a branch is reset so a commit becomes unreachable, collection deletes
  that commit's snapshot, its manifest and the chunks only it referenced, and
  the counters report exactly how many of each.
- `bytes_deleted` counts **bytes freed from storage**, not objects: after
  deleting a single 64-byte chunk plus its metadata it is at least 64 and
  strictly greater than `chunks_deleted`.
- Collection invalidates the caches of the `AssetManager` it was given, so the
  same `Repository` remains usable afterwards: it must still resolve its
  branch, read surviving chunks, and accept new commits.

#### 4.10.2 Expiration accounting — required behaviour

`expire_ref(storage, settings, asset_manager, reference, older_than)` walks the
ancestry of `reference`, treating a snapshot as expired when its `flushed_at`
is **strictly before** `older_than`.

- `ref_is_expired` reports whether the **reference's own tip** is expired. It is
  the plain `flushed_at < older_than` test on that tip and nothing else, and it
  is reported on both result variants. In particular it is `true` even when the
  tip **is** the repository's initial snapshot: the initial snapshot is never
  *released*, but an `older_than` later than it still makes the tip expired.
- The repository's **initial snapshot is never released**: it never appears in
  `released_snapshots` and is never removed from a reference's ancestry,
  whatever its `flushed_at` says.
- The result is `NothingToDo { ref_is_expired }`, and the history is left
  exactly as it was, whenever there is nothing to squash: when the snapshot
  that would be rewritten **is** the initial snapshot, when it is already the
  initial snapshot's direct child, or when nothing in the ancestry is
  expirable. In particular an `older_than` older than every commit in the
  repository must **not** rewrite anything and must **not** create a commit
  loop — it reports `NothingToDo { ref_is_expired: false }`.
- Otherwise the result is `Done { .. }`: the oldest non-expired snapshot —
  **the reference's own tip, when every snapshot in the ancestry is expired** —
  is rewritten so that its parent is the initial snapshot, its id is reported
  as `edited_snapshot`, and every snapshot skipped out of the history is
  reported in `released_snapshots`. Afterwards the reference's ancestry is
  strictly shorter, still ends at the initial snapshot, and still starts at the
  same tip.
- Expiration is irreversible: there is no un-expire operation.

`expire` applies `expire_ref` to every reference in the repository and
aggregates the results. When a reference is expired and the matching
`ExpiredRefAction` is `Delete`, that reference is deleted and reported in
`deleted_refs`; with `Ignore` nothing is deleted. The **default branch `main`
is never deleted**, whatever the branch action says.

Released snapshots are not automatically deleted — `garbage_collect` is what
removes them, and a released snapshot a reference still points at survives.

#### 4.10.3 Failure modes the drivers assert

Every row below must fail, and must fail with the named **kind** where one is
given. Wording is never asserted (section 9).

| Operation | Outcome |
| --- | --- |
| `Repository::open` where no repository exists at the prefix | `RepositoryErrorKind::RepositoryDoesntExist` |
| `Repository::create` where the prefix already holds a repository | `RepositoryErrorKind::ParentDirectoryNotClean` |
| `Repository::create` over a backend whose `can_write()` is `false` | `RepositoryErrorKind::ReadonlyStorage(_)` (section 6 item 10) |
| `Repository::delete_branch("main")` | `RepositoryErrorKind::CannotDeleteMain` |
| `Repository::create_branch` / `create_tag` at a snapshot that does not exist | `Err`, and no reference is written (section 4.3.2) |
| `Repository::lookup_snapshot` of an id never written, or already collected | `Err` |
| `Repository::lookup_branch` / `lookup_tag` of a name that does not exist | `Err` |
| `Store::get` at a key nothing was written at | `StoreErrorKind::NotFound(_)` (section 4.6) |
| Any write on a `Store` over a read-only session | `StoreErrorKind::ReadOnly` (section 4.6) |
| `Session::commit` on a read-only session | `SessionErrorKind::ReadOnlySession` |
| `Session::merge` on a read-only session | `SessionErrorKind::ReadOnlySession` |
| `Session::commit` on a session with no changes | `SessionErrorKind::NoChangesToCommit` |
| `Session::commit` from a session whose branch has moved since it was opened | `SessionErrorKind::Conflict { .. }` |
| `Session::rebase` with a solver that cannot resolve the differences | `SessionErrorKind::RebaseFailed { conflicts, .. }`, whose `conflicts` names the classes of section 4.5 |
| `Session::get_group` / `get_array` / `get_node` for a path with no such node, and `get_group` / `get_array` for a path whose node is of the other kind | `Err` |
| `new_s3_storage` **or** `new_s3_object_store_storage` with a Tigris endpoint, `new_tigris_storage` without a region in the consistent mode, `new_r2_storage` with neither an endpoint nor an account id, or with neither a bucket nor a prefix | `Err` (section 4.8); the two `new_r2_storage` rows carry `StorageErrorKind::R2ConfigurationError` |
| A CLI invocation whose argv does not parse | exit **2** (section 5.3) |
| A CLI invocation whose argv parses but whose work fails | exit **1**, nothing on stdout (section 5.3) |

### 4.11 `icechunk::refs` and `icechunk::virtual_chunks` (inherited)

```rust
// icechunk::refs
#[derive(Debug, Clone, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum Ref { Tag(String), Branch(String) }
impl Ref {
    pub const DEFAULT_BRANCH: &'static str = "main";
    pub fn is_tag(&self) -> bool;
    pub fn is_branch(&self) -> bool;
}
pub enum RefErrorKind { RefNotFound(String), Conflict { .. }, /* … */ }
pub type RefError  = ICError<RefErrorKind>;
pub type RefResult<T> = Result<T, RefError>;

// icechunk::virtual_chunks
pub type ContainerName = String;
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct VirtualChunkContainer { pub name: ContainerName, pub url_prefix: String,
                                   pub store: ObjectStoreConfig }
pub fn mk_default_containers() -> HashMap<ContainerName, VirtualChunkContainer>;
pub struct VirtualChunkResolver { /* … */ }
impl VirtualChunkResolver {
    pub fn matching_container(&self, chunk_location: &str) -> Option<&VirtualChunkContainer>;
    pub async fn fetch_chunk(&self, chunk_location: &str, range: &Range<u64>,
                             checksum: Option<&Checksum>) -> Result<Bytes, VirtualReferenceError>;
}
pub struct ObjectStoreFetcher { /* replaces the previous milestone's LocalFSFetcher */ }
impl ObjectStoreFetcher {
    pub async fn new_gcs(bucket: String, prefix: Option<String>,
                         credentials: Option<GcsCredentials>,
                         config: HashMap<String, String>)
        -> Result<ObjectStoreFetcher, VirtualReferenceError>;
}
```

`mk_default_containers()` returns a **non-empty** map of the containers a
repository starts with; every entry is keyed by its own `name` and carries a
non-empty `url_prefix`. Which containers it holds, and what their prefixes
are, is out of scope.

A virtual chunk reference is matched to a container by **prefix**: the
container whose `url_prefix` the location string starts with. The comparison
is **literal** — `str::starts_with` on the location string and nothing else.
The prefix is neither parsed as a URL nor normalised, any scheme is accepted
and none is privileged, and nothing requires the prefix to end at a separator:
a `url_prefix` of `"file://"` matches `file:///abs/path`, and a `url_prefix`
of `"driver-s3://bucket/"` matches a location under that invented scheme.
`RepositoryConfig::set_virtual_chunk_container` stores the container as given
and validates **no** part of its `url_prefix`, and `Repository::create` keeps
it unchanged. A container whose
`store` is `ObjectStoreConfig::LocalFileSystem(path)` resolves `file://`
locations by reading the named file, honouring the reference's `offset` and
`length`. The `path` payload plays **no part** in that read: a `file://`
virtual reference is an absolute url and is read at the location it names, so
the payload must be present only because the variant carries it — any value,
`PathBuf::new()` included, behaves the same for a virtual container.

---

## 5. The command-line interface

A requirement group drives the compiled binary as a process. Everything it relies on
is here.

### 5.1 Invocation

| | |
| --- | --- |
| Binary name | `icechunk` |
| Built by | `cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk` |
| Found at | `$CARGO_TARGET_DIR/debug/icechunk`, passed to the drivers in `ICECHUNK_TEST_CLI_BIN` |
| Configuration file | `$XDG_CONFIG_HOME/icechunk/cli-config.yaml`, falling back to `$HOME/.config/icechunk/cli-config.yaml` |

The drivers set `XDG_CONFIG_HOME` to a directory they own, so the binary must
resolve its configuration directory from that variable on Linux.

### 5.2 Argument grammar — complete

```text
icechunk <COMMAND>

icechunk repo create <alias>
icechunk snapshot list <REPO> [-n <N>] [-b|--branch <BRANCH>]
icechunk config init [-f|--force]
icechunk config add <alias>
icechunk config list
icechunk help [SUBCOMMAND...]
```

- The three commands are `repo`, `snapshot` and `config`, each with its own
  subcommands; `help` is the fourth entry a help listing shows.
- The only global option is `-h, --help`. **There is no `--version`.**
- `<alias>` and `<REPO>` are repository aliases: any string parses, and the
  failure for an unknown one happens later, at configuration lookup.
- `-n <N>` is a non-negative integer that caps how many snapshots are listed;
  it defaults to **10** and has no long form.
- `-b, --branch <BRANCH>` names the branch to walk; it defaults to **`main`**.
- `-f, --force` belongs to `config init` only.
- `config list` takes no arguments.

### 5.3 Exit statuses

| invocation | stream | exit |
| --- | --- | --- |
| `icechunk --help`, `icechunk <sub> --help` | stdout | **0** |
| any command that succeeds | stdout | **0** |
| no subcommand at all | stderr | **2** |
| an unrecognised subcommand, option, or option value | stderr | **2** |
| `icechunk --version` | stderr | **2** — there is no such option |
| any command that fails at run time | stderr | **1** |

"Fails at run time" means the argv parsed but the work did not: an alias that
is not in the configuration, a repository that already exists, a branch that
does not exist, an unreachable location. These are exit **1**, never 2, and
they print nothing on stdout.

### 5.4 Standard output

**`icechunk --help`** lists the subcommands `repo`, `snapshot`, `config` and
`help`, and documents `-h, --help`. **`icechunk snapshot list --help`**
documents `-n` and `-b, --branch`. Both go to stdout and exit 0. The exact
layout and wording are out of scope.

Section 5.3's table says which stream carries the output that **is** in scope. It
does not make the other stream silent: no group asserts that a successful
invocation writes *nothing* to stderr, so a binary that also emits a log line
or a `tracing` subscriber's output there loses nothing (section 9). The one
stream rule that *is* in scope in the other direction is the one in section 5.3 —
a command that fails at run time prints nothing on **stdout**. It does report
the problem on **stderr**: a run-time failure writes something there. What it
writes is out of scope — no group reads the wording (section 9) — only that the
failure is reported on that stream and not on stdout.

**`icechunk repo create <alias>`** creates the repository at the configured
location and prints a one-line confirmation on stdout. Afterwards the location
holds the repository's `refs/` and `snapshots/` prefixes, the initial snapshot
is written, and `Repository::open` on the same location succeeds with `main`
as its only branch.

**`icechunk snapshot list <REPO>`** prints **one line per snapshot**, newest
first, at most `-n` of them, walking the ancestry of the `-b` branch. Each
line is the Rust `Debug` rendering of that snapshot's `SnapshotInfo`, so each
line:

- begins with `SnapshotInfo`;
- names the fields of `SnapshotInfo` including `parent_id`, `message`,
  `metadata` and — new at this milestone — `manifests`;
- contains the snapshot's commit message.

Nothing else about the line is in scope: no field order, no spacing, no
punctuation, no identifier encoding.

**`icechunk config list`** prints the configuration as YAML on stdout and exits
0. With repositories configured, the output names each alias, its backend kind
and its location. With **no configuration file at all**, or with a file that
does not parse, it prints an **empty** repository map and still exits **0** —
a missing or unreadable configuration is treated as an empty one, never as an
error.

### 5.5 The configuration file

The file is YAML. Its top level is a map with one key, `repos`, whose value
maps an alias to a repository definition. A definition is an **externally
tagged** enum: the variant name is a YAML tag on the value, not a nested key.

```yaml
repos:
  local1: !LocalFileSystem
    path: /some/absolute/path
    config: {}
  s3repo: !S3
    location: { bucket: my-bucket, prefix: my-prefix }
    object_store_config:
      region: us-west-2
      endpoint_url: null
      anonymous: false
      allow_http: false
      force_path_style: false
    credentials: { s3_credential_type: from_env }
    config: {}
  tigrisrepo: !Tigris
    location: { bucket: b, prefix: p }
    object_store_config: { region: r, endpoint_url: "https://fly.storage.tigris.dev",
                           anonymous: false, allow_http: false, force_path_style: false }
    credentials: { s3_credential_type: from_env }
    config: {}
  gcsrepo: !GCS
    location: { bucket: b, prefix: p }
    object_store_config: {}
    credentials: { gcs_credential_type: from_env }
    config: {}
  azrepo: !Azure
    location: { account: a, container: c, prefix: p }
    object_store_config: {}
    credentials: { az_credential_type: from_env }
    config: {}
```

The five variant tags are `!LocalFileSystem`, `!S3`, `!Tigris`, `!GCS` and
`!Azure`, and a requirement group writes a document naming **all five** and expects
`config list` to read every one of them back.

`config:` is a `RepositoryConfig`; because every one of its fields is optional,
`config: {}` is valid and means "all defaults", and it is what the drivers
write. `object_store_config:` is an `S3Options` for `!S3` and `!Tigris` and a
plain string map for `!GCS` and `!Azure`.

`credentials:` is the matching credential enum of `icechunk::config`:
`S3Credentials` for `!S3` and `!Tigris`, `GcsCredentials` for `!GCS`,
`AzureCredentials` for `!Azure`; `!LocalFileSystem` has none. Those enums are
**internally tagged**, not externally tagged like the repository definition
itself: the variant is a key *inside* the credentials map, named
`s3_credential_type`, `gcs_credential_type` or `az_credential_type`, and its
value is the variant's name in `snake_case` — `from_env` for `FromEnv`, which
is what the drivers write. A bare `credentials: FromEnv` is **not** the form
this file uses.

`icechunk config list` reads this document and prints the configuration back in
the same shape, so that re-reading its output yields the same configuration. It
need not be byte-identical: a field the document leaves implicit — every field
of `config: {}` — may be printed out in full.

The corresponding Rust types live in `icechunk::cli::config` behind the `cli`
feature and are not named by any library driver:

```rust
pub struct RepoLocation { pub bucket: String, pub prefix: Option<String> }
pub struct AzureRepoLocation { pub account: String, pub container: String, pub prefix: Option<String> }
pub enum RepositoryDefinition { LocalFileSystem { path: PathBuf, config: RepositoryConfig },
                                S3 { .. }, Tigris { .. }, Azure { .. }, GCS { .. } }
pub struct RepositoryAlias(pub String);
pub struct CliConfig { pub repos: HashMap<RepositoryAlias, RepositoryDefinition> }
```

### 5.6 The interactive subcommands

`config init` and `config add` are **interactive**: they prompt for a
repository alias, a backend kind and its location, then write the file above,
creating its parent directories. `config init` refuses when the existing
configuration already contains repositories and `--force` was not given;
`config add` refuses an alias that already exists. Because they need a
terminal, **no requirement group runs them** (section 9).

---

## 6. Migration from the previous milestone

Every item below breaks code written against milestone 04 (`icechunk` 0.2.0).
Where a signature changed, the form in section 4 is the required one.

**Build and language**

1. `icechunk/Cargo.toml`: `edition = "2021"` → **`edition = "2024"`**.
   Inside an edition-2024 crate, `std::env::set_var` / `remove_var` are
   `unsafe`, and some `impl Trait` returns need explicit `use<…>` capture
   lists.
2. `icechunk` version `0.2.0` → **`0.2.11`**.
3. New `cli` feature and a `[[bin]] icechunk` target with
   `required-features = ["cli"]`; a plain `cargo build` still produces no
   binary.
4. `pub mod cli;` added to `lib.rs`. No module was removed.

**`icechunk::repository`**

5. `VersionInfo` **lost its `Serialize`/`Deserialize` derives** and their
   `rename` attributes. Code that serialised a `VersionInfo` no longer
   compiles.
6. `VersionInfo` gained `AsOf { branch: String, at: DateTime<Utc> }`. The enum
   is `#[non_exhaustive]`.
7. `Repository::resolve_version` changed from private to `pub`.
8. `Repository::create_branch` now **errors** when `snapshot_id` does not
   exist. Code that pointed a branch at a random id and expected `Ok` now gets
   `Err`.
9. `Repository::create_tag` likewise.
10. `Repository::{create, create_branch, create_tag, reset_branch,
    delete_branch, delete_tag, writable_session}` first
    check `storage.can_write()` and return
    `RepositoryErrorKind::ReadonlyStorage(String)` when it is false.
11. `RepositoryErrorKind` gained `InvalidAsOfSpec { branch, at }` and
    `ReadonlyStorage(String)`; it is `#[non_exhaustive]`.
12. `Repository` gained `as_bytes` / `from_bytes` (section 4.3) and a private
    `default_commit_metadata` field that they carry. Milestone 04 had **no**
    repository marshalling at all — only `Session` and `Store` marshalled — so
    nothing from the previous milestone round-trips into this one, and there is
    nothing to migrate.
13. `snapshot_ancestry`, `snapshot_ancestry_arc`, `ancestry` and
    `ancestry_arc` gained precise-capture `use<…>` clauses, and `ancestry`
    gained an explicit `<'a>` lifetime.

**`icechunk::session`**

14. `Session::create_writable_session` gained an **eighth** parameter,
    `default_commit_metadata: SnapshotProperties`.
15. `Session::commit` now merges the repository's default metadata under the
    caller's properties (section 4.3.3). An assertion of the form
    `metadata == my_props` holds only when the repository has no defaults.
16. `Session::discard_changes(&mut self) -> ChangeSet` is new.
17. `ChangeSet::new_nodes_iterator` lost its `+ '_`; several iterator returns
    gained `use<…>`.

**`icechunk::storage`**

18. The `Storage` supertrait list gained `fmt::Display`.
19. `Storage::can_write(&self) -> bool` added as a **required** method.
20. `Storage::delete_batch(&self, prefix: &str, batch: Vec<(String, u64)>)
    -> StorageResult<DeleteObjectsResult>` added as a **required** method;
    `delete_objects` became a *provided* method.
21. `Storage::delete_objects`: `ids` became `BoxStream<'_, (String, u64)>` and
    the return became `StorageResult<DeleteObjectsResult>`.
22. `delete_chunks` / `delete_manifests` / `delete_snapshots` /
    `delete_transaction_logs`: the stream item became `(Id, u64)` and the
    return `DeleteObjectsResult`.
23. `Storage::delete_refs` returns `StorageResult<u64>` instead of `usize`.
24. `ListInfo<Id>` gained the public field `size_bytes: u64`. Struct literals
    without it break. This field is what funds byte accounting.
25. `storage::Settings` gained `unsafe_use_conditional_update`,
    `unsafe_use_conditional_create` and `unsafe_use_metadata`. Struct literals
    break; `Settings::default()` and `Settings::merge` still work. The drivers
    build a `storage::Settings` by struct literal with no `..`, so the type
    must carry **exactly** the four public fields section 4.8 declares and no
    fifth of your own.
26. `StorageErrorKind::Other`'s message changed from
    `"unknown storage error: {0}"` to `"storage error: {0}"`. Nothing may
    assert on either.
27. `StorageErrorKind` gained `R2ConfigurationError(String)`; that enum is
    **not** `#[non_exhaustive]`, so exhaustive matches break.
28. `new_tigris_storage` gained the fifth parameter
    `use_weak_consistency: bool`. Existing calls must pass `false` **and** now
    supply a `region`.
29. `new_s3_storage` and `new_s3_object_store_storage` now reject any
    `endpoint_url` containing `fly.storage.tigris.dev`.
30. `storage::s3::mk_client` gained `extra_read_headers` and
    `extra_write_headers` parameters.
31. `storage::object_store::ObjectStoreBackend`: the supertrait list gained
    `Display`; `mk_object_store` is no longer `async`; `supports_metadata` and
    `supports_conditional_put_updates` were **removed**; `default_settings`
    went from provided to **required**.
32. `ObjectStorage::supports_metadata` and
    `ObjectStorage::supports_conditional_put_updates` were **removed**.
33. `S3Storage`'s serialized form gained `can_write`, `extra_read_headers`,
    `extra_write_headers` and `force_path_style`, so persisted blobs from the
    previous milestone do not deserialize.
34. `Storage::root_is_clean` returns `Err` on a listing error instead of
    reporting `true`.

**`icechunk::config`**

35. `S3Options` gained the public field `force_path_style: bool`. Every struct
    literal of `S3Options` must add it; there is no `Default` impl.
    Deserialization is forgiving — the field defaults to `false` when absent.
36. `S3Options` gained `impl fmt::Display`.
37. `GcsCredentials` gained the variant `Anonymous`; that enum is **not**
    `#[non_exhaustive]`.
38. `RepositoryConfig` itself did **not** change.

**`icechunk::ops`**

39. `ops::gc::all_roots` and `ops::gc::pointed_snapshots` moved to
    `icechunk::ops`, became public, and changed error type: `GCResult<…>` →
    `RefResult<…>` and `RepositoryResult<…>` respectively.
40. `GCSummary` gained the leading field `bytes_deleted: u64`, and all five
    existing counters changed from `usize` to `u64`. Struct literals break, and
    so do comparisons that force `usize` inference.
41. `ExpireRefResult::RefIsExpired` was **removed**.
42. `ExpireRefResult::NothingToDo` became a struct variant
    `NothingToDo { ref_is_expired: bool }`.
43. `ExpireRefResult::Done` gained `ref_is_expired: bool`.
44. `expire_ref` no longer returns early when the reference itself is expired:
    it walks the whole ancestry and reports expiry through the flag, so a case
    that previously reported "the ref is expired" may now return
    `Done { ref_is_expired: true, .. }` *and* rewrite a snapshot.
45. `expire_ref` gained a guard for "we never found an expirable snapshot",
    fixing the commit loop an out-of-range `older_than` used to create. A
    threshold older than every commit now returns
    `NothingToDo { ref_is_expired: false }`.
46. `garbage_collect` now mutates the caches of the `AssetManager` it is
    given (`clear_chunk_cache`, `remove_cached_*`).
47. `ExpireResult` is unchanged.

**`icechunk::format`**

48. `SnapshotInfo` gained the public field `manifests: Vec<ManifestFileInfo>`.
    Struct literals break, and the `Debug` rendering the CLI prints changes
    shape.

**`icechunk::virtual_chunks`**

49. `LocalFSFetcher` and `LocalFSFetcher::new` were **removed**, replaced by
    `ObjectStoreFetcher` (its local constructor is private; `new_gcs` is new
    and public).
50. `VirtualChunkResolver::get_fetcher` changed from `pub` to private;
    `matching_container` and `fetch_chunk` stay public.
51. The sealed `ChunkFetcher` trait replaced `fetch_chunk` with `fetch_part`,
    `ideal_concurrent_request_size` and `max_concurrent_requests_for_object`.

**Unchanged and safe to keep**: `icechunk::error`, `icechunk::refs`,
`icechunk::strategies`, `format::mod`, `format::manifest`,
`format::transaction_log`, `format::attributes`, every `conflicts` module, and
`AssetManager::new_no_cache`. The on-disk format did not change: the
specification document is byte-identical to the previous milestone's.

---

## 7. Formats crossing the boundary

| What | Form |
| --- | --- |
| Commit metadata | `BTreeMap<String, serde_json::Value>`; values are arbitrary JSON |
| Snapshot / manifest / chunk ids | opaque `ObjectId` values; only equality and `random()` are used |
| Session marshalling | `Vec<u8>` produced and consumed by `Session::as_bytes`/`from_bytes`; the encoding is yours, but within one build it must round-trip the session's uncommitted change set, its branch name and its base snapshot id |
| Repository marshalling | `Vec<u8>` produced and consumed by `Repository::as_bytes`/`from_bytes`. It carries a **description of the backend**, not the backend's contents, so a revived repository reaches the same storage only when that description is enough to rebuild it — true of the local-filesystem backend, and not of the in-memory one, which revives empty. A repository revived over a local-filesystem backend must keep its configuration and resolve its references. Bytes do not have to be portable between builds, or between milestones |
| Timestamps | `chrono::DateTime<Utc>`; only ordering relative to other generated timestamps, and comparison against a caller-supplied threshold, is observed |
| CLI configuration file | YAML, exactly as section 5.5 spells it |
| CLI snapshot listing | one line per snapshot as section 5.4 spells it |

---

## 9. Out of scope at this milestone

- **Any network, cloud or container backend.** No group contacts S3, GCS,
  Azure, R2, Tigris or MinIO, reads a cloud credential, or requires a running
  service. The cloud constructors are exercised for their **configuration**
  behaviour only.
- **Object storage classes.** No storage-class API exists at this release and
  none is required. "Object-store-specific configuration" means
  `S3Options::force_path_style`, the Tigris consistency mode, `new_r2_storage`,
  and the per-backend configuration maps — nothing else.
- **A Rust `Repository::total_chunks_storage`.** That name belongs to the
  Python bindings. The Rust surface for the same question is the free function
  `icechunk::ops::stats::repo_chunks_storage`, and it is the only one in scope.
- **New `RepositoryConfig` fields.** This milestone adds none; nothing asserts
  any.
- **`icechunk::cli`'s Rust types.** No library group names
  `icechunk::cli::config` or `icechunk::cli::interface`; the command line is
  checked only through the compiled binary.
- **The interactive `config init` and `config add`.** They need a terminal and
  no group runs them.
- **`icechunk-python`, `icechunk-js`, Dask, Xarray and the Python CLI entry
  point.** The verifier builds neither and asserts nothing about them.
- **Message text.** No group asserts a `Display` or `Debug` string, an error
  message, a help-text layout, or log output. Error *categories* — the
  `…ErrorKind` variants named in section 4 — are asserted; their wording is
  not. **Two things sit beside this rule rather than under it**, exactly as
  `PROFILE.md` P3 lists them:
    - the **structural** properties of the command line's own standard output,
      which section 5.4 fixes. A `snapshot list` line *is* the `Debug`
      rendering of a `SnapshotInfo`, and its prefix, the field names it must
      contain, the number of lines and their order are asserted — never the
      layout, spacing, punctuation, field order or wording;
    - the negative property that a `Storage` rendering must **not** contain a
      credential (section 4.8).

  `impl fmt::Display for S3Options` is required by section 4.7 and is therefore
  exercised, but only for its **existence**: a group renders an `S3Options` and
  asserts the rendering is not empty. What it says is yours.
- **`tracing` output.** Expiration and collection emit logging upstream;
  nothing here observes it.
- **Internal layout**: module file names under `src/`, private types, caching
  strategy, allocation, concurrency, wall-clock timing, and your own tests.
- **Version strings.** No group reads a version out of a manifest.
- **Build-shape facts that live only in the manifests.** The crate's edition,
  its version, whether `Cargo.lock` is consistent with the manifests, whether a
  plain `cargo build` produces **no** binary, and whether `pub mod cli;` is
  declared unconditionally are all stated in sections 1 and 1.1 and in
  `WORKSPACE_HANDOFF.md` — and the verifier's **two build commands are the only
  check on them**. No requirement group inspects a manifest or `lib.rs`, so a
  candidate whose `icechunk` package turns the `cli` feature on by default, or
  whose `cli` module is itself feature-gated, loses nothing here as long as both
  build commands succeed. They are still obligations of the handoff; they are
  not behaviour this milestone checks.
- **The `AsOf` version selector, `Repository::diff`, `Store::set_virtual_refs`
  and `getsize_prefix`** are part of the public surface but are out of scope at
  this milestone.
- **Declared types whose behaviour no document here fixes.** The following are
  part of the public surface sections 4.7, 4.8, 4.5 and 4.11 declare, and they
  must exist with the shapes given there, but **no group asserts anything about
  them**, because the only assertion available would be that the name resolves:
  `storage::DeleteObjectsResult`, `new_gcs_storage` and
  `new_azure_blob_storage` (section 4.8's outcome table covers the S3, Tigris
  and R2 constructors only; for GCS and Azure the sole statement here is that
  no constructor performs I/O), `format::manifest::Checksum`,
  `conflicts::ConflictResolution` (the `ConflictSolver` trait's own method
  signature is deliberately not fixed here, so nothing can call it directly),
  `refs::RefErrorKind` (reachable only through a transparent wrapper this
  document does not name), and `virtual_chunks::ObjectStoreFetcher` with its
  `new_gcs`. `ConcurrencySettings`' own fields and defaults are likewise not
  fixed: `Settings::concurrency()` is exercised, what it returns is not.
