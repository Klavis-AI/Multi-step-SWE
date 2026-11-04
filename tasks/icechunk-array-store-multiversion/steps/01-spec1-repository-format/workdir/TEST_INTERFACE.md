# TEST_INTERFACE.md — milestone 01 (the spec-1 repository format)

This is the public contract the hidden verifier holds you to. Everything the
verifier names — crate identity, build command, module paths, exported symbols,
signatures, the formats crossing the boundary, and the crates it links against
— appears here, or in `/app/icechunk-spec/` in a document `instruction.md`'s
reading list names.

The verifier may combine these interfaces in ways this document does not
enumerate; it will not use an interface this document does not name.

**Section 8 lists what is out of scope at this milestone.** Read it: several
things `icechunk-spec/spec.md` defines are deliberately outside this
milestone's contract, and `icechunk-spec/PROFILE.md` resolves seven places
where `spec.md` and this milestone disagree outright. Where `spec.md` and
`PROFILE.md` conflict, `PROFILE.md` wins.

## 1. Crate identity and build contract

| Item | Value |
| --- | --- |
| Package name | `icechunk` |
| Package version | `0.1.0-alpha.1` |
| Library manifest | `/app/icechunk/Cargo.toml` |
| Workspace root manifest | `/app/Cargo.toml` — a `[workspace]` whose `members` contains `"icechunk"` and whose `default-members` is `["icechunk"]` |
| Crate root | `/app/icechunk/src/lib.rs` |
| Lockfile | `/app/Cargo.lock` — present and kept consistent with the manifests |
| Crate type | Rust library (`lib`), edition `2021` |
| Build command | `cargo build` run in `/app` |
| Toolchain | the `rustc`/`cargo` in the image, **1.98.1**; no nightly features, no `rust-toolchain.toml` pinning something else |

The verifier runs `cargo build` in `/app` first. That build must succeed and
must build the `icechunk` library; if it fails, every requirement group of this
milestone fails. `cargo build` in `/app` builds the workspace's
**default members**, so keep `default-members = ["icechunk"]`. You may add
further workspace members, but nothing outside `default-members` may be needed
for the library to build, and no member may require a toolchain, SDK or
interpreter the image does not have.

Then the verifier creates a **throw-away driver crate outside `/app`** that
declares

```toml
icechunk = { path = "/app/icechunk" }
```

and compiles its own integration tests against your crate. Your manifests are
never modified, your `Cargo.lock` is never rewritten in place, and **no test of
yours is ever run or read as evidence**.

The drivers are built with a **task-owned lockfile** first, `--offline
--locked`, so a candidate that left the manifests alone resolves nothing and
downloads nothing. If that resolution does not build, the verifier falls back to
your `/app/Cargo.lock` as a seed, completed by one resolver run; if that fails
too, it re-resolves from scratch. Keep `Cargo.lock` in the workspace and keep it
consistent with `Cargo.toml`: the fallbacks are a rescue path, not a guarantee,
and a dependency you add is fetched at check time.

## 2. Workspace layout

```text
/app/
  Cargo.toml            the workspace root       (required; default-members = ["icechunk"])
  Cargo.lock            your resolved graph      (required)
  icechunk/
    Cargo.toml          the library manifest     (required, name = "icechunk")
    src/lib.rs          the crate root           (required)
    src/...             your implementation      (layout below lib.rs is yours)
  icechunk-spec/        the specification corpus (read-only; never edit)
  TEST_INTERFACE.md     this file
  WORKSPACE_HANDOFF.md  what to preserve when this milestone ends
  timer.sh              advisory countdown for this milestone's budget
```

Module organisation **below** `lib.rs` is yours, except for the module paths
section 4 names: those are part of the public API and the drivers write them
verbatim (`icechunk::format::manifest::ChunkPayload`, and so on). Which file a
given item lives in is out of scope.

## 3. Crates the verifier links against

The driver crate declares exactly these dependencies and no others:

```toml
icechunk    = { path = "/app/icechunk" }
bytes       = { version = "1", features = ["serde"] }
futures     = "0.3"
serde_json  = "1"
tempfile    = "3"
tokio       = { version = "1", features = ["rt-multi-thread", "macros"] }
```

Consequences you must design for:

- **`bytes::Bytes` crosses the API boundary** — in `ChunkPayload::Inline`, in
  `Store::get`/`set`, in `UserAttributes::to_bytes`, in `ByteRange::slice` and
  in the chunk reader and writer. Your crate must depend on a
  **semver-compatible** `bytes` (1.x). A different major version makes the two
  `Bytes` types distinct and the drivers will not compile. The same holds of
  `futures` (0.3.x) for `Stream`, and of `serde_json` (1.x) for
  `serde_json::Value`.
- **`serde_json::Value` crosses the boundary** in `UserAttributes::parsed`,
  `Codec::configuration`, `StorageTransformer::configuration`,
  `SnapshotProperties` and `FillValue::from_data_type_and_json`.
- **Streams are `futures::Stream`.** Every method section 4 types as a stream
  must return something the driver can drive with
  `futures::TryStreamExt::try_collect`.
- **Async entry points run on plain tokio**, single- or multi-threaded. Do not
  require a particular executor, a runtime flavour, or a background task the
  caller must start. Several drivers use `#[tokio::test(flavor =
  "multi_thread")]`; several use the current-thread flavour. Both must work.
- **The drivers never implement your traits.** `Storage` and
  `VirtualChunkResolver` are sealed (section 4.7), so there is no mock backend;
  every driver runs against your own in-memory or local-filesystem backend.
- **The drivers unwrap your `Result`s and format failures with `{}` and
  `{:?}`**, so every public error type must implement `std::fmt::Debug` and
  `std::fmt::Display`. Where section 4 annotates a type with standard traits
  (`Debug`, `Clone`, `PartialEq`, …), those impls are required. Section 8
  excludes the **text** `Debug` and `Display` produce; it never excludes the
  impls themselves.
- `tempfile::TempDir` provides the scratch directories: your local-filesystem
  backend must accept a plain absolute `std::path::Path` such as
  `/tmp/.tmpXYZ/repo`, and your virtual-reference resolver must read a
  `file:///tmp/.tmpABC/chunk-1` URL.

## 4. The public API required at this milestone

### 4.1 Crate root and module map

`/app/icechunk/src/lib.rs` must expose at least these module paths and
re-exports; the names are what the drivers import. A further public module of
your own is neither required nor forbidden.

```rust
pub mod change_set;
pub mod format;
pub mod metadata;
pub mod refs;
pub mod repository;
pub mod storage;
pub mod zarr;

pub use repository::{Repository, RepositoryBuilder, RepositoryConfig, SnapshotMetadata};
pub use storage::{MemCachingStorage, ObjectStorage, Storage, StorageError};
pub use zarr::Store;
```

Note that `icechunk::RepositoryConfig` (= `repository::RepositoryConfig`, 4.6)
and `icechunk::zarr::RepositoryConfig` (4.9) are **two different types** with
the same name. A driver that needs both aliases one of them.

### 4.2 `icechunk::format` — paths, ids, coordinates, byte ranges

```rust
pub struct Path(/* private */);                  // Debug, Clone, PartialEq, Eq,
                                                 // PartialOrd, Ord, Hash, serde
impl Path {
    pub fn root() -> Path;
    pub fn new(path: &str) -> Result<Path, PathError>;
    pub fn starts_with(&self, other: &Path) -> bool;
    pub fn ancestors(&self) -> impl Iterator<Item = Path> + '_;
}
impl TryFrom<&str>    for Path { type Error = PathError; }
impl TryFrom<&String> for Path { type Error = PathError; }
impl TryFrom<String>  for Path { type Error = PathError; }
impl std::fmt::Display for Path {}

#[non_exhaustive]
pub enum PathError { NotAbsolute, NotCanonic }   // Debug, Clone, PartialEq, Eq
```

- A node path is a **`/`-separated absolute UNIX path**. `Path::root()` is `/`
  and `Path::root().to_string()` is `"/"`.
- A path that does not begin with `/` is `Err(PathError::NotAbsolute)`.
- A path that is not canonical — one containing a `..` component, such as
  `/a/../b`, `/a/b/..` or `/..` — is `Err(PathError::NotCanonic)`.
  A `.` component is **out of scope** either way (section 8).
- `Display` returns the path unchanged: `Path::new("/a/b")?.to_string()` is
  `"/a/b"`.
- `starts_with` compares **whole components**: `/a/b` starts with `/a` and with
  `/`, and `/ab` does **not** start with `/a`.
- `ancestors()` yields the path itself first and then each parent, ending at the
  root: for `/a/b` it is `["/a/b", "/a", "/"]`.

```rust
pub trait FileTypeTag: /* sealed */ {}
pub struct SnapshotTag;  pub struct ManifestTag;
pub struct ChunkTag;     pub struct AttributesTag;

pub struct ObjectId<const SIZE: usize, T: FileTypeTag>(pub [u8; SIZE], /* private */);
// Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Hash, Display, serde

pub type SnapshotId   = ObjectId<12, SnapshotTag>;
pub type ManifestId   = ObjectId<12, ManifestTag>;
pub type ChunkId      = ObjectId<12, ChunkTag>;
pub type AttributesId = ObjectId<12, AttributesTag>;

impl<const SIZE: usize, T: FileTypeTag> ObjectId<SIZE, T> {
    pub fn random() -> Self;
    pub fn new(buf: [u8; SIZE]) -> Self;
    pub const FAKE: Self;                        // all-zero
}
impl TryFrom<&[u8]> for ObjectId<SIZE, T> { type Error = &'static str; }
impl TryFrom<&str>  for ObjectId<SIZE, T> { type Error = &'static str; }
impl From<&ObjectId<SIZE, T>> for String {}
```

- The **first tuple field is public**, so a driver reads `id.0` as `[u8; 12]`.
  The second field is private, so struct-literal construction from outside is
  impossible — `new`, `random`, `FAKE` and the two `TryFrom`s are the only ways
  in.
- The string form is **Crockford base32** over the id's bytes, and it is also
  the serde form (a JSON string, not an array). Concretely:
  `SnapshotId::new([0,1,2,3,4,5,6,7,8,9,10,11])` renders as
  `"000G40R40M30E209185G"` through `String::from(&id)`, through `Display`, and
  through `serde_json::to_string`, and `SnapshotId::try_from` of that string
  returns the same id.
- `TryFrom<&[u8]>` accepts exactly `SIZE` bytes and fails otherwise;
  `TryFrom<&str>` fails when the decoded byte length is wrong.
- `random()` draws a fresh id each call.

```rust
pub struct ChunkIndices(pub Vec<u32>);  // Debug, Clone, PartialEq, Eq,
                                        // PartialOrd, Ord, Hash, serde
pub type NodeId      = u32;
pub type ChunkOffset = u64;
pub type ChunkLength = u64;
pub type TableOffset = u32;

pub struct ByteRange(pub std::ops::Bound<ChunkOffset>,
                     pub std::ops::Bound<ChunkOffset>);   // Debug, Clone, PartialEq, Eq, Hash
impl ByteRange {
    pub fn from_offset(offset: ChunkOffset) -> Self;              // [offset, ..)
    pub fn from_offset_with_length(offset: ChunkOffset,
                                   length: ChunkOffset) -> Self;  // [offset, offset+length)
    pub fn to_offset(offset: ChunkOffset) -> Self;                // (.., offset)
    pub fn bounded(start: ChunkOffset, end: ChunkOffset) -> Self; // [start, end)
    pub fn length(&self) -> Option<u64>;                          // None when unbounded above
    pub const ALL: Self;                                          // (.., ..)
    pub fn slice(&self, bytes: Bytes) -> Bytes;
}
impl From<(Option<ChunkOffset>, Option<ChunkOffset>)> for ByteRange {}
```

`length()` is the number of bytes the range covers — `Some(4)` for
`bounded(2, 6)` — and `None` when the range is unbounded above, as `ALL` and
`from_offset(n)` are.

`slice` is the reference semantics for every byte range in this milestone:
`ByteRange::ALL.slice(b)` is `b`, `bounded(s, e).slice(b)` is `b[s..e]`,
`from_offset(s).slice(b)` is `b[s..]`, `to_offset(e).slice(b)` is `b[..e]`.
Reading a chunk through a byte range must return exactly what `slice` would.

```rust
#[non_exhaustive]
pub enum IcechunkFormatError {            // Debug, Clone, PartialEq, Eq, Error
    FillValueDecodeError { found_size: usize, target_size: usize, target_type: DataType },
    FillValueParse { data_type: DataType, value: serde_json::Value },
    NodeNotFound { path: Path },
    ChunkCoordinatesNotFound { coords: ChunkIndices },
}
pub type IcechunkResult<T> = Result<T, IcechunkFormatError>;
```

`format` also declares the submodules `attributes`, `manifest` and `snapshot`.

### 4.3 `format::snapshot` and `format::manifest` — nodes, snapshots, chunks

```rust
// icechunk::format::snapshot
pub enum NodeType { Group, Array }                      // Debug, Clone, PartialEq, Eq
pub struct UserAttributesRef { pub object_id: AttributesId, pub location: TableOffset }
pub enum UserAttributesSnapshot { Inline(UserAttributes), Ref(UserAttributesRef) }

pub struct ZarrArrayMetadata {                          // Debug, Clone, PartialEq, serde
    pub shape: ArrayShape,
    pub data_type: DataType,
    pub chunk_shape: ChunkShape,
    pub chunk_key_encoding: ChunkKeyEncoding,
    pub fill_value: FillValue,
    pub codecs: Vec<Codec>,
    pub storage_transformers: Option<Vec<StorageTransformer>>,
    pub dimension_names: Option<DimensionNames>,
}

pub enum NodeData { Array(ZarrArrayMetadata, Vec<ManifestRef>), Group }

pub struct NodeSnapshot {                               // Debug, Clone, PartialEq, serde
    pub id: NodeId,
    pub path: Path,
    pub user_attributes: Option<UserAttributesSnapshot>,
    pub node_data: NodeData,
}
impl NodeSnapshot { pub fn node_type(&self) -> NodeType; }

pub struct SnapshotMetadata {                           // Debug, Clone, PartialEq, Eq, serde
    pub id: SnapshotId,
    pub written_at: chrono::DateTime<chrono::Utc>,
    pub message: String,
}
pub type SnapshotProperties = std::collections::HashMap<String, serde_json::Value>;

pub struct Snapshot { /* fields are yours */ }
impl Snapshot {
    pub const INITIAL_COMMIT_MESSAGE: &'static str;
}
```

`ZarrArrayMetadata` and `NodeSnapshot` are **struct-literal constructed** by the
drivers, so those fields are public and named exactly as above.
`NodeSnapshot::node_type()` is `Group` for a `NodeData::Group` and `Array` for a
`NodeData::Array`. `Snapshot::INITIAL_COMMIT_MESSAGE` is the commit message the
empty snapshot that `Repository::init` writes carries; the drivers compare
against **the constant**, never against a literal, so its text is yours. Only
the constant's existence and its use as the initial message are in scope — the
rest of `Snapshot`'s surface is not (section 8).

```rust
// icechunk::format::manifest
pub struct ManifestExtents(pub Vec<ChunkIndices>);      // Debug, Clone, PartialEq, Eq, serde
pub struct ManifestRef { pub object_id: ManifestId, pub extents: ManifestExtents }

#[non_exhaustive]
pub enum VirtualChunkLocation { Absolute(String) }      // Debug, Clone, PartialEq, Eq,
                                                        // PartialOrd, Ord, Hash, serde
impl VirtualChunkLocation {
    pub fn from_absolute_path(path: &str)
        -> Result<VirtualChunkLocation, VirtualReferenceError>;
}

pub struct VirtualChunkRef {                            // Debug, Clone, PartialEq, Eq,
    pub location: VirtualChunkLocation,                 // PartialOrd, Ord, Hash, serde
    pub offset: ChunkOffset,
    pub length: ChunkLength,
}
pub struct ChunkRef {                                   // same traits
    pub id: ChunkId,
    pub offset: ChunkOffset,
    pub length: ChunkLength,
}

#[non_exhaustive]
pub enum ChunkPayload {                                 // same traits
    Inline(Bytes),
    Virtual(VirtualChunkRef),
    Ref(ChunkRef),
}

pub struct ChunkInfo {                                  // Debug, Clone, PartialEq, Eq,
    pub node: NodeId,                                   // PartialOrd, Ord, Hash
    pub coord: ChunkIndices,
    pub payload: ChunkPayload,
}

#[non_exhaustive]
pub enum VirtualReferenceError {                        // Debug, Display, Error
    CannotParseUrl(url::ParseError),
    NoPathSegments(String),
    UnsupportedScheme(String),
    CannotParseBucketName(String),
    FetchError(Box<dyn std::error::Error + Send + Sync>),
    OtherError(Box<dyn std::error::Error + Send + Sync>),
}
```

`ChunkPayload`'s three variants are **the three chunk reference kinds** of
`spec.md` "Chunk Manifest Files", and the drivers construct all three by name.
`#[non_exhaustive]` on these enums means an external `match` needs a `_` arm; it
does not stop a driver constructing a named variant, and the drivers do.

`VirtualChunkLocation::from_absolute_path` takes an absolute **URL**:

- a string that is not a URL — `"abcdef"` — is
  `Err(VirtualReferenceError::CannotParseUrl(_))`;
- an `s3` URL with no host — `"s3:///foo/path"` — is
  `Err(VirtualReferenceError::CannotParseBucketName(_))`;
- `"file:///tmp/x/chunk"` and `"s3://bucket/key"` succeed, as does a URL whose
  scheme is neither (`"ftp://example/chunk"`): an unsupported scheme is rejected
  when the chunk is **read**, not when the location is built (4.6, section 6).

`format::attributes` declares `pub struct AttributesTable {}` and nothing else
is required of it (PROFILE.md P7).

### 4.4 `icechunk::change_set`

```rust
pub struct ChangeSet { /* private */ }        // Debug, Clone, PartialEq, Default, serde
impl ChangeSet {
    pub fn is_empty(&self) -> bool;
    pub fn export_to_bytes(&self) -> RepositoryResult<Vec<u8>>;
    pub fn import_from_bytes(bytes: &[u8]) -> RepositoryResult<Self>;
    pub fn merge(&mut self, other: ChangeSet);
    pub fn merge_many<T: IntoIterator<Item = ChangeSet>>(&mut self, others: T);
}
impl From<Repository> for ChangeSet {}        // consumes the repository
```

A `ChangeSet` is the **uncommitted work of one writing session**: the groups and
arrays it added, updated or deleted, the user attributes it set, and the chunk
references it wrote. `ChangeSet::default()` is empty and `is_empty()` is true of
it; a session that added anything is not empty.

`export_to_bytes` and `import_from_bytes` must round-trip **each other**: for
any `ChangeSet` `c`, `ChangeSet::import_from_bytes(&c.export_to_bytes()?)?`
equals `c`. The byte format is yours (PROFILE.md P6); bytes that are not one of
yours — arbitrary text, and the empty slice — must be rejected rather than
silently decoded.

`merge(other)` folds another session's change set into this one: the groups,
arrays, attribute updates, deletions and chunk references `other` holds become
this change set's as well, and where both name the same item `other` wins.
`merge_many(others)` merges each change set of a sequence in turn. Merging into
a `ChangeSet::default()` therefore yields a change set equal to the one merged
in, and a repository resumed through `RepositoryBuilder::with_change_set` with
the merge of two sessions' change sets sees both sessions' work.

The full set of per-item accessors and mutators on `ChangeSet` exists in the
crate but is out of scope at this milestone (section 8); the drivers reach a
change set only through the five methods above, `From<Repository>`,
`Repository::change_set_bytes`, `RepositoryBuilder::with_change_set`,
`Repository::distributed_commit` and `Store::change_set_bytes`.

### 4.5 `icechunk::metadata`

```rust
pub type ArrayShape     = Vec<u64>;
pub type DimensionName  = Option<String>;
pub type DimensionNames = Vec<DimensionName>;

pub struct Codec { pub name: String,
                   pub configuration: Option<HashMap<String, serde_json::Value>> }
pub struct StorageTransformer { pub name: String,
                   pub configuration: Option<HashMap<String, serde_json::Value>> }
pub struct ChunkShape(pub Vec<std::num::NonZeroU64>);
pub enum ChunkKeyEncoding { Slash, Dot, Default }        // Copy, Clone, Debug, PartialEq, Eq, serde
impl TryFrom<u8> for ChunkKeyEncoding { type Error = &'static str; }   // b'/', b'.', b'x'
impl From<ChunkKeyEncoding> for u8 {}

pub struct UserAttributes { pub parsed: serde_json::Value }   // Debug, Clone, PartialEq, Eq, serde
impl UserAttributes {
    pub fn try_new(json: &[u8]) -> Result<UserAttributes, serde_json::Error>;
    pub fn to_bytes(&self) -> Bytes;
}

#[non_exhaustive]
pub enum DataType {                                       // Debug, Clone, PartialEq, Eq, serde
    Bool, Int8, Int16, Int32, Int64, UInt8, UInt16, UInt32, UInt64,
    Float16, Float32, Float64, Complex64, Complex128, String, Bytes,
}
impl DataType {
    pub fn fits_i64(&self, n: i64) -> bool;
    pub fn fits_u64(&self, n: u64) -> bool;
}
impl TryFrom<&str> for DataType { type Error = &'static str; }
impl std::fmt::Display for DataType {}

pub enum FillValue {                                      // Debug, Clone, PartialEq, serde
    Bool(bool), Int8(i8), Int16(i16), Int32(i32), Int64(i64),
    UInt8(u8), UInt16(u16), UInt32(u32), UInt64(u64),
    Float16(f32), Float32(f32), Float64(f64),
    Complex64(f32, f32), Complex128(f64, f64),
    String(String), Bytes(Vec<u8>),
}
impl FillValue {
    pub const NAN_STR: &'static str;       // "NaN"
    pub const INF_STR: &'static str;       // "Infinity"
    pub const NEG_INF_STR: &'static str;   // "-Infinity"
    pub fn from_data_type_and_json(dt: &DataType, value: &serde_json::Value)
        -> Result<Self, IcechunkFormatError>;
    pub fn get_data_type(&self) -> DataType;
}
```

`DataType`'s textual form — the one `TryFrom<&str>`, `Display` and serde all use
— is the lower-case Zarr name: `bool`, `int8` … `uint64`, `float16`,
`float32`, `float64`, `complex64`, `complex128`, `string`, `bytes`. A name
outside that set must fail to parse.

`DataType::fits_i64(n)` / `fits_u64(n)` report whether the integer `n` is
representable in that data type — `Int8` fits `127` and not `128`, `Int64` fits
every `i64`, and a type that is not an integer of that signedness fits none.

`FillValue::from_data_type_and_json(dt, value)` reads a JSON fill value against
a data type: a number for a numeric type, a bool for `Bool`, and — for the
three floats — the strings `NAN_STR`, `INF_STR` and `NEG_INF_STR` for the
non-finite values. A value the data type cannot hold is an
`Err(IcechunkFormatError)`; which variant reports it is out of scope (section 6).
`FillValue::get_data_type()` returns the `DataType` of the variant it is.

`UserAttributes::try_new` parses a JSON document into `parsed`; `to_bytes`
serialises it back. The two round-trip each other as *documents*: reading
`to_bytes()` back as JSON yields `parsed`. The byte layout `to_bytes` chooses —
whitespace and member order — is out of scope. Input that is not JSON must fail.

### 4.6 `icechunk::repository`

```rust
pub struct RepositoryConfig {                 // Debug, Clone
    pub inline_chunk_threshold_bytes: u16,
    pub unsafe_overwrite_refs: bool,
}
impl Default for RepositoryConfig {}          // { 512, false }  — PROFILE.md P8

pub struct RepositoryBuilder { /* private; `new` is private */ }   // Debug, Clone
impl RepositoryBuilder {
    pub fn with_inline_threshold_bytes(&mut self, threshold: u16) -> &mut Self;
    pub fn with_unsafe_overwrite_refs(&mut self, value: bool) -> &mut Self;
    pub fn with_config(&mut self, config: RepositoryConfig) -> &mut Self;
    pub fn with_virtual_ref_config(
        &mut self, config: ObjectStoreVirtualChunkResolverConfig) -> &mut Self;
    pub fn with_change_set(&mut self, change_set: ChangeSet) -> &mut Self;
    pub fn build(&self) -> Repository;
}

pub struct Repository { /* private */ }       // Debug
```

Every setter takes `&mut self` and returns `&mut Self`; `build` takes `&self`.
The drivers chain them on a temporary —
`Repository::init(storage, false).await?.with_inline_threshold_bytes(1).build()`
— so the chain must work without an intermediate binding.

`RepositoryBuilder::new` is private. These six associated functions are the
**only** ways to obtain a builder or to ask about a repository's existence:

```rust
impl Repository {
    pub fn update(storage: Arc<dyn Storage + Send + Sync>,
                  previous_version_snapshot_id: SnapshotId) -> RepositoryBuilder;   // sync
    pub async fn from_branch_tip(storage: Arc<dyn Storage + Send + Sync>,
                  branch_name: &str) -> RepositoryResult<RepositoryBuilder>;
    pub async fn from_tag(storage: Arc<dyn Storage + Send + Sync>,
                  tag_name: &str) -> RepositoryResult<RepositoryBuilder>;
    pub async fn init(storage: Arc<dyn Storage + Send + Sync>,
                  unsafe_overwrite_refs: bool) -> RepositoryResult<RepositoryBuilder>;
    pub async fn exists(storage: &(dyn Storage + Send + Sync)) -> RepositoryResult<bool>;
    pub fn add_in_mem_asset_caching(storage: Arc<dyn Storage + Send + Sync>)
        -> Arc<dyn Storage + Send + Sync>;                                          // sync
}
```

- `init` writes one **empty snapshot** whose commit message is
  `Snapshot::INITIAL_COMMIT_MESSAGE`, then creates the default branch
  (`Ref::DEFAULT_BRANCH`, `"main"`) pointing at it. Afterwards `exists` is true,
  `list_refs` reports exactly that one branch, the repository has no uncommitted
  changes and no nodes, and `ancestry` yields that single snapshot.
- `init` on a storage that already holds a repository is
  `Err(RepositoryError::AlreadyInitialized)`.
- `exists` is false for a storage with no repository, and is not an error.
- `from_branch_tip` / `from_tag` fail when the reference does not exist.
- `add_in_mem_asset_caching` wraps a backend in an in-memory cache and changes
  no observable behaviour.
- `RepositoryBuilder::with_change_set(cs)` starts the repository with `cs`
  already pending: `has_uncommitted_changes()` is then true when `cs` is not
  empty, the nodes and chunk references it holds are visible through `get_node`
  and `get_chunk_ref`, and committing writes them. That is how a session
  exported with `Repository::change_set_bytes` (4.4) is resumed.

```rust
impl Repository {
    pub fn storage(&self) -> &Arc<dyn Storage + Send + Sync>;                 // sync
    pub fn snapshot_id(&self) -> &SnapshotId;                                 // sync
    pub fn has_uncommitted_changes(&self) -> bool;                            // sync
    pub async fn ancestry(&self)
        -> RepositoryResult<impl Stream<Item = RepositoryResult<SnapshotMetadata>>>;

    pub async fn add_group(&mut self, path: Path) -> RepositoryResult<()>;
    pub async fn delete_group(&mut self, path: Path) -> RepositoryResult<()>;
    pub async fn add_array(&mut self, path: Path, metadata: ZarrArrayMetadata)
        -> RepositoryResult<()>;
    pub async fn update_array(&mut self, path: Path, metadata: ZarrArrayMetadata)
        -> RepositoryResult<()>;
    pub async fn delete_array(&mut self, path: Path) -> RepositoryResult<()>;
    pub async fn set_user_attributes(&mut self, path: Path,
        atts: Option<UserAttributes>) -> RepositoryResult<()>;
    pub async fn set_chunk_ref(&mut self, path: Path, coord: ChunkIndices,
        data: Option<ChunkPayload>) -> RepositoryResult<()>;

    pub async fn get_node(&self, path: &Path) -> RepositoryResult<NodeSnapshot>;
    pub async fn get_array(&self, path: &Path) -> RepositoryResult<NodeSnapshot>;
    pub async fn get_group(&self, path: &Path) -> RepositoryResult<NodeSnapshot>;
    pub async fn get_chunk_ref(&self, path: &Path, coords: &ChunkIndices)
        -> RepositoryResult<Option<ChunkPayload>>;
    pub async fn get_chunk_reader(&self, path: &Path, coords: &ChunkIndices,
        byte_range: &ByteRange)
        -> RepositoryResult<Option<
               Pin<Box<dyn Future<Output = RepositoryResult<Bytes>> + Send>>>>;
    pub fn get_chunk_writer(&self)                                            // sync
        -> impl FnOnce(Bytes)
               -> Pin<Box<dyn Future<Output = RepositoryResult<ChunkPayload>> + Send>>;

    pub async fn clear(&mut self) -> RepositoryResult<()>;
    pub async fn list_nodes(&self)
        -> RepositoryResult<impl Iterator<Item = NodeSnapshot> + '_>;
    pub async fn all_chunks(&self)
        -> RepositoryResult<impl Stream<Item = RepositoryResult<(Path, ChunkInfo)>> + '_>;

    pub async fn flush(&mut self, message: &str, properties: SnapshotProperties)
        -> RepositoryResult<SnapshotId>;
    pub async fn distributed_flush<I: IntoIterator<Item = ChangeSet>>(&mut self,
        other_change_sets: I, message: &str, properties: SnapshotProperties)
        -> RepositoryResult<SnapshotId>;
    pub async fn commit(&mut self, update_branch_name: &str, message: &str,
        properties: Option<SnapshotProperties>) -> RepositoryResult<SnapshotId>;
    pub async fn distributed_commit<I: IntoIterator<Item = ChangeSet>>(&mut self,
        update_branch_name: &str, other_change_sets: I, message: &str,
        properties: Option<SnapshotProperties>) -> RepositoryResult<SnapshotId>;

    pub fn change_set_bytes(&self) -> RepositoryResult<Vec<u8>>;              // sync
    pub async fn new_branch(&self, branch_name: &str) -> RepositoryResult<BranchVersion>;
    pub async fn tag(&self, tag_name: &str, snapshot_id: &SnapshotId)
        -> RepositoryResult<()>;
}

pub async fn get_chunk(
    reader: Option<Pin<Box<dyn Future<Output = RepositoryResult<Bytes>> + Send>>>,
) -> RepositoryResult<Option<Bytes>>;        // free function in `repository`
```

Behaviour the drivers require:

- **`storage()`** hands back the backend the repository was built on, so
  `Repository::exists(repo.storage().as_ref())` is true of an initialised
  repository.
- **Adding.** `add_group` / `add_array` on a path that already holds a node is
  `Err(AlreadyExists)`; the error carries the existing `NodeSnapshot`. After
  adding, `get_node` finds it, `get_group`/`get_array` agree with its kind, and
  `has_uncommitted_changes()` is true.
- **Kinds.** `get_array` on a group is `Err(NotAnArray)`, `get_group` on an
  array is `Err(NotAGroup)`, and either on a path with no node is
  `Err(NodeNotFound)`. `update_array` on a missing path is `Err(NodeNotFound)`.
- **Deleting** is idempotent and never fails for a missing node:
  `delete_group` / `delete_array` on a path that holds nothing succeed.
- **User attributes.** `set_user_attributes(path, Some(a))` makes
  `get_node(path)?.user_attributes` `Some(..)`; passing `None` clears it back to
  `None`. On a missing path it is an error.
- **Chunk references.** `set_chunk_ref(path, coord, Some(payload))` records the
  payload; `get_chunk_ref` returns exactly what was recorded.
  `set_chunk_ref(path, coord, None)` deletes the reference, after which
  `get_chunk_ref` is `Ok(None)` — as it is for a coordinate never written.
  `set_chunk_ref` on a group is `Err(NotAnArray)` and on a missing path
  `Err(NodeNotFound)`.
- **Reading a chunk.** `get_chunk_reader` returns `Ok(None)` when there is no
  reference, and otherwise a future that yields the referenced bytes windowed by
  the `ByteRange` (4.2). `get_chunk(reader)` is the helper that awaits it:
  `None` in, `Ok(None)` out. All three payload kinds are readable this way — an
  inline payload from its own bytes, a `Ref` from the chunk the repository
  wrote, a `Virtual` from its `file://` location (4.3, PROFILE.md P10).
- **Writing a chunk.** `get_chunk_writer()` returns a closure that consumes
  `Bytes` and yields a `ChunkPayload`: `ChunkPayload::Ref(ChunkRef { offset: 0,
  length: data.len(), .. })` when `data.len() > inline_chunk_threshold_bytes`,
  and `ChunkPayload::Inline(data)` otherwise — **exactly** the threshold is
  still inline (PROFILE.md P8). The chunk it materialises is readable through
  `get_chunk_reader`.
- **`flush`** writes a new snapshot from the accumulated change set, clears the
  change set, makes `snapshot_id()` the new id — and **touches no reference**.
  The new snapshot is readable through `Repository::update`.
- **`commit(branch, message, properties)`** flushes and then moves `branch` from
  the repository's own snapshot to the new one, atomically. On success it
  returns the new id, `snapshot_id()` is that id, `has_uncommitted_changes()` is
  false, and the branch tip is that id.
- **Committing nothing** — no accumulated changes — is
  `Err(NoChangesToCommit)`, for both `commit` and `flush`, and moves no
  reference.
- **A stale writer loses.** Two repositories opened at the same branch tip that
  both accumulate changes: the first `commit` succeeds, the second is
  `Err(Conflict { expected_parent, actual_parent })` where `expected_parent` is
  the parent the loser held and `actual_parent` is the winner's new snapshot.
  The branch then holds the winner's work and only the winner's work.
- **`distributed_commit`** merges the supplied change sets with its own before
  flushing, so a chunk written in another session and carried across as a
  `ChangeSet` is readable from the coordinator after the commit.
- **`ancestry`** yields `SnapshotMetadata` **newest first** — the current
  snapshot, then each parent, ending with the snapshot `init` wrote.
- **`new_branch(name)`** creates `name` pointing at the repository's current
  snapshot and returns `BranchVersion(0)`; committing to that branch afterwards
  leaves every other branch where it was.
- **`tag(name, id)`** creates an immutable tag; creating one that exists is an
  error (section 6).
- **`list_nodes`** enumerates every node of the current version including the
  uncommitted ones; **`all_chunks`** enumerates every chunk reference with the
  path of the array it belongs to. Neither order is fixed (PROFILE.md P13).
- **`clear`** deletes every node; after it, `list_nodes` is empty, and the
  emptiness survives a commit and a reopen.

```rust
#[non_exhaustive]
pub enum RepositoryError { /* section 6 */ }   // Debug, Display, Error
pub type RepositoryResult<T> = Result<T, RepositoryError>;
```

`repository` also re-exports, for convenience, `ChangeSet`, `ChunkPayload`,
`VirtualChunkLocation`, `SnapshotMetadata`, `ZarrArrayMetadata`, `ChunkIndices`,
`Path` and the whole of `metadata`. Those re-exports are **out of scope** — the
drivers use the canonical paths of 4.2 to 4.5 — but keeping them costs nothing.

### 4.7 `icechunk::storage`

```rust
pub mod caching;   pub mod object_store;   pub mod s3;   pub mod virtual_ref;
pub use caching::MemCachingStorage;
pub use object_store::ObjectStorage;

#[async_trait::async_trait]
pub trait Storage: std::fmt::Debug + /* sealed */ {
    async fn fetch_snapshot(&self, id: &SnapshotId) -> StorageResult<Arc<Snapshot>>;
    async fn fetch_attributes(&self, id: &AttributesId) -> StorageResult<Arc<AttributesTable>>;
    async fn fetch_manifests(&self, id: &ManifestId) -> StorageResult<Arc<Manifest>>;
    async fn fetch_chunk(&self, id: &ChunkId, range: &ByteRange) -> StorageResult<Bytes>;
    async fn write_snapshot(&self, id: SnapshotId, table: Arc<Snapshot>) -> StorageResult<()>;
    async fn write_attributes(&self, id: AttributesId, table: Arc<AttributesTable>)
        -> StorageResult<()>;
    async fn write_manifests(&self, id: ManifestId, table: Arc<Manifest>) -> StorageResult<()>;
    async fn write_chunk(&self, id: ChunkId, bytes: Bytes) -> StorageResult<()>;
    async fn get_ref(&self, ref_key: &str) -> StorageResult<Bytes>;
    async fn ref_names(&self) -> StorageResult<Vec<String>>;
    async fn ref_versions(&self, ref_name: &str)
        -> StorageResult<BoxStream<StorageResult<String>>>;
    async fn write_ref(&self, ref_key: &str, overwrite_refs: bool, bytes: Bytes)
        -> StorageResult<()>;
}

pub enum StorageError { /* variants are yours */ }   // Debug, Display, Error
pub type StorageResult<A> = Result<A, StorageError>;
```

`Storage` is **sealed**: a downstream crate cannot implement it, and none tries.
The drivers name it only as `Arc<dyn Storage + Send + Sync>` and
`&(dyn Storage + Send + Sync)`, so the trait must stay object safe and the trait
object must be `Send + Sync`. The exact method set above is what the
`Repository` and `refs` layers are built on; a driver never calls a `Storage`
method directly, so the signatures matter only in that the trait must exist,
must be public, and must be nameable in those two forms.

```rust
// icechunk::storage::object_store
pub struct ObjectStorage { /* private */ }            // Debug
impl ObjectStorage {
    pub fn new_in_memory_store(prefix: Option<String>) -> ObjectStorage;            // sync
    pub fn new_local_store(prefix: &std::path::Path)
        -> Result<ObjectStorage, std::io::Error>;                                   // sync
}

// icechunk::storage::caching
pub struct MemCachingStorage { /* private */ }
impl MemCachingStorage {
    pub fn new(backend: Arc<dyn Storage + Send + Sync>,
               num_snapshots: u16, num_manifests: u16,
               num_attributes: u16, num_chunks: u16) -> Self;                       // sync
}

// icechunk::storage::s3  — constructible and deserialisable, never connected
pub struct S3Storage { /* private */ }
pub struct StaticS3Credentials { pub access_key_id: String,
                                 pub secret_access_key: String,
                                 pub session_token: Option<String> }
pub enum S3Credentials { FromEnv, Anonymous, Static(StaticS3Credentials) }   // Default = FromEnv
pub struct S3Config { pub region: Option<String>, pub endpoint: Option<String>,
                      pub credentials: S3Credentials, pub allow_http: bool }

// icechunk::storage::virtual_ref
pub trait VirtualChunkResolver: std::fmt::Debug + /* sealed */ {
    async fn fetch_chunk(&self, location: &VirtualChunkLocation, range: &ByteRange)
        -> Result<Bytes, VirtualReferenceError>;
}
pub enum ObjectStoreVirtualChunkResolverConfig { S3(S3Config) }   // Clone, Debug, PartialEq,
                                                                  // Eq, serde
```

Both backends must offer **conditional create-if-not-exists** for reference
files: that is the single point at which `spec.md` "Reference Files" places
atomicity, and every branch and tag guarantee in 4.5 and 4.6 rests on it.
`new_local_store` creates its directory if it does not exist.
`new_in_memory_store`'s `prefix` namespaces keys inside one store; two calls
always yield two independent, empty repositories.

`S3Config`, `S3Credentials` and `StaticS3Credentials` are `serde` types and are
named by `zarr::StorageConfig` (4.9); nothing else about S3 is in scope
(PROFILE.md P9).

### 4.8 `icechunk::refs`

```rust
pub enum Ref { Tag(String), Branch(String) }     // Debug, Clone, PartialEq, Eq, Hash
impl Ref { pub const DEFAULT_BRANCH: &'static str; }   // "main"

pub struct BranchVersion(pub u64);               // Debug, Clone, PartialEq, Eq
pub struct RefData { pub snapshot: SnapshotId }  // Debug, Clone, PartialEq, Eq, serde

pub enum RefError {                              // Debug, Display, Error — NOT non_exhaustive
    Storage(StorageError),
    RefNotFound(String),
    InvalidRefType(String),
    InvalidRefName(String),
    InvalidBranchVersion(String),
    TagAlreadyExists(String),
    Serialization(serde_json::Error),
    Conflict { expected_parent: Option<SnapshotId>, actual_parent: Option<SnapshotId> },
}
pub type RefResult<A> = Result<A, RefError>;

pub async fn create_tag(storage: &(dyn Storage + Send + Sync), name: &str,
    snapshot: SnapshotId, overwrite_refs: bool) -> RefResult<()>;
pub async fn update_branch(storage: &(dyn Storage + Send + Sync), name: &str,
    new_snapshot: SnapshotId, current_snapshot: Option<&SnapshotId>,
    overwrite_refs: bool) -> RefResult<BranchVersion>;
pub async fn list_refs(storage: &(dyn Storage + Send + Sync)) -> RefResult<Vec<Ref>>;
pub async fn fetch_tag(storage: &(dyn Storage + Send + Sync), name: &str)
    -> RefResult<RefData>;
pub async fn fetch_branch_tip(storage: &(dyn Storage + Send + Sync), name: &str)
    -> RefResult<RefData>;
pub async fn fetch_ref(storage: &(dyn Storage + Send + Sync), ref_name: &str)
    -> RefResult<(Ref, RefData)>;
```

`Ref` is matched exhaustively by a driver, so it has exactly those two variants.
`RefError` is matched by variant but never exhaustively; you may add variants.

- `Ref::DEFAULT_BRANCH` is the string `"main"`, and that is the branch
  `Repository::init` creates.
- **`update_branch(.., current_snapshot = None, ..)` creates** a branch. It
  returns `BranchVersion(0)` on success, and
  `Err(RefError::Conflict { expected_parent: None, actual_parent: Some(tip) })`
  when the branch already exists.
- **`update_branch(.., current_snapshot = Some(&id), ..)` is a compare-and-swap**
  against the branch's current tip. On success it returns the previous version
  plus one — `BranchVersion(1)`, `(2)`, … — and on a tip that is not `id` it is
  `Err(RefError::Conflict { expected_parent: Some(id), actual_parent })` where
  `actual_parent` is the real tip. A refused update leaves the tip alone.
  Naming a `current_snapshot` for a branch that does not exist is also a
  conflict, not a create.
- **`fetch_branch_tip`** returns the `RefData` of the latest version of a
  branch, and `Err(RefError::RefNotFound(_))` for a branch that does not exist.
- **`create_tag`** creates an immutable tag. Creating one whose name already
  exists is `Err(RefError::TagAlreadyExists(_))` **whatever snapshot it names**,
  and the existing tag is unchanged. `fetch_tag` returns its `RefData`, and
  `Err(RefError::RefNotFound(_))` for a name that does not exist.
- **A reference name may not contain `/`** — for branches and tags alike:
  `Err(RefError::InvalidRefName(_))`.
- **`fetch_ref(name)`** resolves a bare name, preferring a tag over a branch,
  and returns which kind it found: `(Ref::Tag(name), data)` or
  `(Ref::Branch(name), data)`. A name that is neither is an error.
- **`list_refs`** returns every reference in the repository, branches and tags
  alike, in no fixed order (PROFILE.md P13).
- `overwrite_refs` is the escape hatch of PROFILE.md P8: `false` means the write
  is conditional. Every driver passes `false`.

Reading an arbitrary historical branch version is **not** part of this
milestone's public API and is out of scope.

### 4.9 `icechunk::zarr` — the Zarr store and its configuration

```rust
pub use crate::format::ObjectId;

#[non_exhaustive]
#[serde(tag = "type")]
pub enum StorageConfig {                          // Debug, Clone, PartialEq, Eq, serde
    #[serde(rename = "in_memory")]        InMemory { prefix: Option<String> },
    #[serde(rename = "local_filesystem")] LocalFileSystem { root: std::path::PathBuf },
    #[serde(rename = "s3")]               S3ObjectStore { bucket: String, prefix: String,
                                              #[serde(flatten)] config: Option<S3Config> },
}
impl StorageConfig {
    pub async fn make_storage(&self) -> Result<Arc<dyn Storage + Send + Sync>, String>;
    pub async fn make_cached_storage(&self) -> Result<Arc<dyn Storage + Send + Sync>, String>;
}

#[non_exhaustive]
pub enum VersionInfo {                            // Debug, Clone, PartialEq, Eq, serde
    #[serde(rename = "snapshot_id")] SnapshotId(SnapshotId),
    #[serde(rename = "tag")]         TagRef(String),
    #[serde(rename = "branch")]      BranchTipRef(String),
}

pub struct RepositoryConfig {                     // Debug, Clone, PartialEq, Eq, Default, serde
    pub version: Option<VersionInfo>,
    pub inline_chunk_threshold_bytes: Option<u16>,
    pub unsafe_overwrite_refs: Option<bool>,
    pub change_set_bytes: Option<Vec<u8>>,
    pub virtual_ref_config: Option<ObjectStoreVirtualChunkResolverConfig>,
}
impl RepositoryConfig {
    pub fn new() -> Self;                         // == Default::default()
    pub fn existing(version: VersionInfo) -> Self;
    pub fn with_version(self, version: VersionInfo) -> Self;
    pub fn with_inline_chunk_threshold_bytes(self, threshold: u16) -> Self;
    pub fn with_unsafe_overwrite_refs(self, unsafe_overwrite_refs: bool) -> Self;
    pub fn with_virtual_ref_credentials(self,
        config: ObjectStoreVirtualChunkResolverConfig) -> Self;
    pub fn with_change_set_bytes(self, change_set_bytes: Vec<u8>) -> Self;
    pub async fn make_repository(&self, storage: Arc<dyn Storage + Send + Sync>)
        -> Result<(Repository, Option<String>), String>;
}

pub struct StoreOptions { pub get_partial_values_concurrency: u16 }  // Default = 10;
                                                                    // Debug, Clone, PartialEq, serde
pub struct ConsolidatedStore {                    // Debug, Clone, PartialEq, serde
    pub storage: StorageConfig,
    pub repository: RepositoryConfig,
    pub config: Option<StoreOptions>,
}
impl ConsolidatedStore {
    pub fn with_version(self, version: VersionInfo) -> Self;
    pub fn with_change_set_bytes(self, change_set: Vec<u8>) -> RepositoryResult<Self>;
}

#[non_exhaustive]
pub enum AccessMode {                             // Debug, Clone, PartialEq, Eq, serde
    #[serde(rename = "r")]  ReadOnly,
    #[serde(rename = "rw")] ReadWrite,
}

#[non_exhaustive]
pub enum KeyNotFoundError {                       // Debug, Clone, PartialEq, Eq, Error
    ChunkNotFound { key: String, path: Path, coords: ChunkIndices },
    NodeNotFound { path: Path },
}

#[non_exhaustive]
pub enum StoreError { /* section 6 */ }           // Debug, Display, Error
pub type StoreResult<A> = Result<A, StoreError>;

pub struct Store { /* private */ }                // Debug
```

`StorageConfig`, `RepositoryConfig`, `StoreOptions` and `ConsolidatedStore` are
serde types, and the serde names above are the ones a JSON document uses. Their
`Option` members are all omitted when absent and accepted as absent or `null`
when reading, so `{"storage":{"type":"in_memory"},"repository":{}}` is a valid
`ConsolidatedStore` with `repository == RepositoryConfig::new()` and
`config == None`. A `storage.type` outside the three names must fail to
deserialise. Section 5 gives a fully populated document.

`StorageConfig::make_storage` builds the backend the variant describes;
`make_cached_storage` builds it wrapped in the in-memory asset cache. Both
report failure as a `String`, not a structured error. `S3ObjectStore` is never
built by a driver (PROFILE.md P9).

```rust
impl Store {
    pub async fn from_consolidated(consolidated: &ConsolidatedStore, mode: AccessMode)
        -> Result<Self, String>;
    pub async fn from_json(json: &[u8], mode: AccessMode) -> Result<Self, String>;
    pub async fn new_from_storage(storage: Arc<dyn Storage + Send + Sync>)
        -> Result<Self, String>;
    pub fn from_repository(repository: Repository, mode: AccessMode,             // sync
        current_branch: Option<String>, config: Option<StoreOptions>) -> Self;
    pub fn with_access_mode(&self, mode: AccessMode) -> Self;                    // sync
    pub fn access_mode(&self) -> &AccessMode;                                    // sync
    pub fn current_branch(&self) -> &Option<String>;                             // sync
    pub async fn snapshot_id(&self) -> SnapshotId;                               // infallible
    pub async fn current_version(&self) -> VersionInfo;                          // infallible
    pub async fn has_uncommitted_changes(&self) -> bool;                         // infallible
    pub async fn reset(&mut self) -> StoreResult<()>;
    pub async fn checkout(&mut self, version: VersionInfo) -> StoreResult<()>;
    pub async fn new_branch(&mut self, branch: &str)
        -> StoreResult<(SnapshotId, BranchVersion)>;
    pub async fn commit(&mut self, message: &str) -> StoreResult<SnapshotId>;
    pub async fn distributed_commit<I: IntoIterator<Item = Vec<u8>>>(&mut self,
        message: &str, other_changesets_bytes: I) -> StoreResult<SnapshotId>;
    pub async fn tag(&mut self, tag: &str, snapshot_id: &SnapshotId) -> StoreResult<()>;
    pub async fn ancestry(&self)
        -> StoreResult<impl Stream<Item = StoreResult<SnapshotMetadata>> + Send>;
    pub async fn change_set_bytes(&self) -> StoreResult<Vec<u8>>;
    pub async fn empty(&self) -> StoreResult<bool>;
    pub async fn clear(&mut self) -> StoreResult<()>;

    pub async fn get(&self, key: &str, byte_range: &ByteRange) -> StoreResult<Bytes>;
    pub async fn get_partial_values(&self,
        key_ranges: impl IntoIterator<Item = (String, ByteRange)>)
        -> StoreResult<Vec<StoreResult<Bytes>>>;
    pub async fn exists(&self, key: &str) -> StoreResult<bool>;
    pub fn supports_writes(&self) -> StoreResult<bool>;                          // sync
    pub fn supports_deletes(&self) -> StoreResult<bool>;                         // sync
    pub async fn set(&self, key: &str, value: Bytes) -> StoreResult<()>;         // &self
    pub async fn set_if_not_exists(&self, key: &str, value: Bytes) -> StoreResult<()>;
    pub async fn set_virtual_ref(&mut self, key: &str, reference: VirtualChunkRef)
        -> StoreResult<()>;                                                      // &mut self
    pub async fn delete(&self, key: &str) -> StoreResult<()>;                    // &self
    pub fn supports_partial_writes(&self) -> StoreResult<bool>;                  // sync
    pub async fn set_partial_values(&self,
        key_start_values: impl IntoIterator<Item = (&str, ChunkOffset, Bytes)>)
        -> StoreResult<()>;
    pub fn supports_listing(&self) -> StoreResult<bool>;                         // sync
    pub async fn list(&self)
        -> StoreResult<impl Stream<Item = StoreResult<String>> + Send>;
    pub async fn list_prefix(&self, prefix: &str)
        -> StoreResult<impl Stream<Item = StoreResult<String>> + Send>;
    pub async fn list_dir(&self, prefix: &str)
        -> StoreResult<impl Stream<Item = StoreResult<String>> + Send>;
}
```

Mind which methods take `&self` and which `&mut self`: the table above is the
contract, and a driver calls `set`, `set_if_not_exists` and `delete` on a
non-`mut` binding.

Behaviour the drivers require:

- **`new_from_storage`** initialises a repository on the supplied backend and
  returns a read-write store whose `current_branch()` is `Some("main")`.
  **`from_json` / `from_consolidated`** do the same from a configuration
  document: with no `version`, they initialise and land on `main`; with a
  `version`, they open at it.
- **`from_repository`** wraps a repository the caller already has; the
  `current_branch` argument is what `current_branch()` then reports, and
  `config` defaults to `StoreOptions::default()`.
- **`with_access_mode`** returns another handle onto **the same repository**
  with a different mode. `access_mode()` reports the handle's mode.
- **Capabilities** are constants at this milestone: `supports_writes()` and
  `supports_deletes()` and `supports_listing()` are `Ok(true)`, and
  `supports_partial_writes()` is `Ok(false)`.
- **A read-only store refuses a write** with `StoreError::ReadOnly` — `set`,
  `set_virtual_ref`, `delete` and `set_partial_values` — and still serves `get`,
  `exists` and the listings. (`set_if_not_exists` on a key that already
  resolves writes nothing in either mode; what it returns **in read-only mode**
  is out of scope.)
- **`set_partial_values`** is `Err(StoreError::Unimplemented(_))` on a
  read-write store, whatever it is passed.
- **`set_if_not_exists`** writes only when the key does not already resolve; on
  a key that does, it is a silent no-op and the old value survives.
- **`set_virtual_ref`** works on a chunk key only: on a metadata key it is
  `Err(StoreError::NotAllowed(_))`, and on a key that is neither it is
  `Err(StoreError::InvalidKey { .. })`.
- **`get_partial_values`** returns one result per requested key, **in the order
  requested** (PROFILE.md P13).
- **The three listings.** `list()` yields **every** key in the store — one
  metadata key per node and one chunk key per chunk reference.
  `list_prefix(p)` yields the keys under `p`, matching **whole path
  components**: `group/` matches `group/zarr.json` and `group/array/zarr.json`
  but not `group-suffix/zarr.json`, and the prefix `/` matches everything.
  `list_dir(p)` yields the **immediate children** of `p` — the distinct next
  path component of every key under `p`, each once; `list_dir("/")` names the
  top level, and `p` with and without a trailing `/` name the same directory.
  A chunk key contributes its `c` component and then each coordinate in turn, so
  `list_dir("array")` is `["c", "zarr.json"]` and `list_dir("array/c/")` is the
  set of first coordinates. None of the three has a fixed order
  (PROFILE.md P13); every driver sorts before comparing.
- **`empty()`** is true exactly when the store holds no node. **`exists(key)`**
  is `Ok(false)` — not an error — for a well-formed key that resolves to
  nothing, and propagates the error for a key that is not well formed.
- **`checkout(version)`** repoints the store. `VersionInfo::BranchTipRef(b)`
  sets `current_branch()` to `Some(b)`; `SnapshotId` and `TagRef` **detach** it,
  setting `current_branch()` to `None`. `current_version()` then reports
  `BranchTipRef` when attached and `SnapshotId(snapshot_id())` when detached.
  Checking out a version that does not exist is an error.
- **`checkout` and `new_branch` refuse to run with uncommitted changes**:
  `Err(StoreError::UncommittedChanges)`. `reset()` discards them and returns the
  store to its current snapshot.
- **`commit(message)`** commits to the store's current branch and returns the
  new snapshot id; a **detached** store cannot commit —
  `Err(StoreError::NotOnBranch)`.
- **`new_branch(b)`** creates `b` at the store's current snapshot, switches the
  store onto it, and returns that snapshot id with `BranchVersion(0)`.
- **`tag(name, id)`** creates an immutable tag on any snapshot.
- **`ancestry`** yields the same sequence as `Repository::ancestry`, newest
  first.
- **`clear()`** removes every node; the store then lists nothing, and still
  lists nothing after a commit and after being reopened at that snapshot.

## 5. Formats and values crossing the boundary

### 5.1 The Zarr key grammar

A store key is a `/`-separated relative key, as a Zarr v3 client writes it.

| Key shape | Means |
| --- | --- |
| `zarr.json` | the metadata of the **root** node, `/` |
| `<path>/zarr.json` | the metadata of the node at `/<path>` |
| `c` | the chunk of the root array at the empty coordinate |
| `c/<i>/<j>/…` | the chunk of the root array at `(i, j, …)` |
| `<path>/c` | the chunk of `/<path>` at the empty coordinate |
| `<path>/c/<i>/<j>/…` | the chunk of `/<path>` at `(i, j, …)` |

Coordinates are decimal `u32`s separated by `/`. Anything else is
`Err(StoreError::InvalidKey { key })` carrying the key verbatim — in particular
a key that neither ends in `/zarr.json` nor contains a `c` component (`"foo"`,
`"array/foo"`), and a chunk key whose coordinates are not decimal numbers
(`"array/c/0/x"`).

Deleting a chunk key is idempotent: deleting a chunk that is not there
succeeds. Deleting a metadata key removes the node, after which reading it is
`Err(StoreError::NotFound(KeyNotFoundError::NodeNotFound { path }))`; reading a
chunk key whose chunk is absent is
`Err(StoreError::NotFound(KeyNotFoundError::ChunkNotFound { key, path, coords }))`
with the key verbatim, the array's path and the parsed coordinates.

### 5.2 Group and array metadata documents

A metadata key carries a **Zarr v3 JSON document**. The store parses it into the
node it describes, and reading the key back **re-serialises** the node — so the
round trip is normalising, not byte preserving. Two normalisations are in scope:

- a group written as `{"zarr_format":3, "node_type":"group"}` reads back as
  `{"zarr_format":3,"node_type":"group","attributes":null}` — members in that
  order, whitespace removed, an explicit null `attributes`;
- `attributes` is a free-form JSON object whose **members come back sorted by
  key**: `{"spam":"ham", "eggs":42}` reads back as
  `{"eggs":42,"spam":"ham"}`.

A group document's `node_type` must be the word `group`; an array document's
must be `array`. A document that is not JSON, whose `node_type` is neither, or
which is missing a member the node needs, is `Err(StoreError::BadMetadata(_))`
and creates no node.

An array document carries, in this member order:

```json
{"zarr_format":3,"node_type":"array","attributes":{"foo":42},"shape":[2,2,2],
 "data_type":"int32",
 "chunk_grid":{"name":"regular","configuration":{"chunk_shape":[1,1,1]}},
 "chunk_key_encoding":{"name":"default","configuration":{"separator":"/"}},
 "fill_value":0,
 "codecs":[{"name":"mycodec","configuration":{"foo":42}}],
 "storage_transformers":[{"name":"mytransformer","configuration":{"bar":43}}],
 "dimension_names":["x","y","t"]}
```

- `shape` is `ArrayShape`; `data_type` is the `DataType` text of 4.5.
- `chunk_grid` maps to `ChunkShape`: name `regular`, configuration
  `{"chunk_shape": [...]}` of non-zero integers.
- `chunk_key_encoding` maps to `ChunkKeyEncoding`: name `default`,
  configuration `{"separator": "/"}`.
- `fill_value` is read against `data_type` through
  `FillValue::from_data_type_and_json`, and written back as JSON — with the
  three special strings `"NaN"`, `"Infinity"` and `"-Infinity"` for the
  non-finite floats.
- `codecs` and `storage_transformers` are opaque name/configuration pairs, kept
  and returned, never executed. `storage_transformers` is omitted when absent.
- `dimension_names` is a list whose members may be null.

A document already in that member order, with single-member or already-sorted
`attributes`, reads back **byte for byte**. This is the document the drivers
write, and reading it back must return exactly the bytes that were written.

### 5.3 Byte ranges

`Store::get(key, range)` returns the value windowed by `range`, with the
semantics `ByteRange::slice` defines (4.2) — for metadata documents and for
chunks of every payload kind alike.

### 5.4 A configuration document

```json
{"storage": {"type": "local_filesystem", "root": "/tmp/test"},
 "repository": {
    "version": {"snapshot_id": "000G40R40M30E209185G"},
    "inline_chunk_threshold_bytes": 128,
    "unsafe_overwrite_refs": true
 },
 "config": {"get_partial_values_concurrency": 100}}
```

This is a `ConsolidatedStore`. Note that a `VersionInfo` is an **externally
tagged** object with one member — `snapshot_id`, `tag` or `branch` — and that a
snapshot id inside it is the Crockford base32 string of 4.2.

## 6. Errors

The drivers assert an error's **category**, never its message, its `Display` or
`Debug` text, or its source chain (PROFILE.md P12). These are the categories,
and the operations that must produce them.

`repository::RepositoryError` — `#[non_exhaustive]`:

| Variant | Produced by |
| --- | --- |
| `AlreadyInitialized` | `Repository::init` on an initialised storage |
| `NodeNotFound { path, message }` | `get_node`/`get_array`/`get_group`/`update_array`/`set_user_attributes`/`set_chunk_ref` on a path with no node |
| `NotAnArray { node, message }` | `get_array` or `set_chunk_ref` on a group |
| `NotAGroup { node, message }` | `get_group` on an array |
| `AlreadyExists { node, message }` | `add_group` or `add_array` on an occupied path |
| `NoChangesToCommit` | `commit` or `flush` with nothing accumulated |
| `Conflict { expected_parent, actual_parent }` | a commit whose branch has moved since the repository was opened |
| `Ref(RefError)` | a reference operation that failed — in particular `Repository::tag` on an existing tag, which is `Ref(RefError::TagAlreadyExists(_))` |
| `VirtualReferenceError(VirtualReferenceError)` | reading a virtual chunk whose location cannot be resolved, including an unsupported scheme |
| `StorageError`, `FormatError`, `OtherFlushError`, `Tag`, `SerializationError`, `DeserializationError` | present in the enum; not asserted |

`refs::RefError` — **not** `#[non_exhaustive]` for matching purposes, but you
may add variants:

| Variant | Produced by |
| --- | --- |
| `RefNotFound(name)` | `fetch_tag`, `fetch_branch_tip`, `fetch_ref` on a name that does not exist |
| `Conflict { expected_parent, actual_parent }` | `update_branch` whose `current_snapshot` does not match the tip — including a create against an existing branch (`expected_parent` is then `None`) |
| `TagAlreadyExists(name)` | `create_tag` on an existing tag |
| `InvalidRefName(name)` | any reference name containing `/` |
| `Storage`, `InvalidRefType`, `InvalidBranchVersion`, `Serialization` | present in the enum; not asserted |

`zarr::StoreError` — `#[non_exhaustive]`:

| Variant | Produced by |
| --- | --- |
| `InvalidKey { key }` | a key the grammar of 5.1 does not accept, carrying the key verbatim |
| `NotFound(KeyNotFoundError::NodeNotFound { path })` | reading a metadata key with no node |
| `NotFound(KeyNotFoundError::ChunkNotFound { key, path, coords })` | reading a chunk key with no chunk |
| `BadMetadata(serde_json::Error)` | a metadata document that does not parse |
| `ReadOnly` | any write on a read-only store |
| `NotAllowed(String)` | `set_virtual_ref` on a metadata key |
| `Unimplemented(&'static str)` | `set_partial_values` |
| `UncommittedChanges` | `checkout` or `new_branch` with pending changes |
| `NotOnBranch` | `commit` on a detached store |
| `RepositoryError`, `NoSnapshot`, `BadKeyPrefix`, `PartialValuesPanic`, `Unknown` | present in the enum; not asserted |

`format::PathError`, `format::IcechunkFormatError`,
`manifest::VirtualReferenceError` and `storage::StorageError` are named in
section 4; only `PathError`'s two variants and `VirtualReferenceError`'s
`CannotParseUrl` and `CannotParseBucketName` are asserted.

`StorageConfig::make_storage`, `make_cached_storage`,
`RepositoryConfig::make_repository`, `Store::from_json`,
`Store::from_consolidated` and `Store::new_from_storage` report failure as a
plain `String`, not a structured error. That is deliberate and is part of the
signature.

## 7. Determinism and defaults

- **Defaults.** `RepositoryConfig::default()` is
  `{ inline_chunk_threshold_bytes: 512, unsafe_overwrite_refs: false }`.
  `StoreOptions::default()` is `{ get_partial_values_concurrency: 10 }`.
  `zarr::RepositoryConfig::new()` is `Default::default()` — every member `None`.
  `Ref::DEFAULT_BRANCH` is `"main"`. `AccessMode` has no default; every
  constructor that does not take one produces `ReadWrite`.
- **No wall clock is observed.** `SnapshotMetadata::written_at` and a snapshot's
  `started_at` exist and must be populated, but no driver compares one to a
  clock or to another timestamp.
- **No fixed order where the specification fixes none** (PROFILE.md P13). The
  drivers sort keys, directory children, reference lists and node paths before
  comparing. Two orders are fixed and asserted: ancestry is newest first, and
  `get_partial_values` answers in request order.
- **No unsynchronised concurrency.** Every requirement group is a single
  sequential sequence of operations; where two writers race for a branch, the
  drivers serialise them and assert that the second one loses.
- **Randomness.** `ObjectId::random()` must produce a fresh id per call; a
  driver asserts that thirty-two draws are distinct. Nothing else about the
  distribution is in scope.

## 8. What is out of scope at this milestone

Out of scope at every milestone:

- Internal module layout below the paths section 4 names, private type and
  function names, and which file anything lives in under `src/`.
- Error message wording, error-variant identity where section 6 promises only a
  category, and the **text** `Debug`/`Display` produce. Whether those impls
  exist is not in this list: sections 3 and 4 require them.
- Iteration order the specification does not fix, allocation strategy, timing,
  memory use, throughput.
- Your own tests, benchmarks, examples, documentation and CI configuration. The
  verifier compiles its own drivers and never runs yours.

Additionally, out of scope at **this** milestone:

- **The Zarr array format itself.** Codecs and storage transformers are carried
  as opaque name/configuration pairs and never executed; no chunk is ever
  compressed, decompressed, decoded or interpreted as an array; no fill value is
  ever materialised; data types are names, not layouts; and partial decoding of
  a chunk does not exist. Only the byte ranges of 5.3 are in scope.
- **Chunk grids other than `regular` and chunk key separators other than `/`.**
  `ChunkKeyEncoding::Dot` and `::Default` must exist as variants and convert to
  and from their bytes, but no store key uses them.
- **Real cloud object stores and their credentials.** `S3Storage`,
  `storage::s3::mk_client`, `range_to_header`, `StorageConfig::S3ObjectStore`
  and every `S3Credentials` value must exist, be public and deserialise, but
  nothing connects, no credential is read, and no driver reaches the network
  (PROFILE.md P9). There is no GCS or Azure backend at this milestone.
- **Resolving an `s3://` virtual chunk reference.** Only `file://` is resolved
  (PROFILE.md P10).
- **The Python binding.** `icechunk-python`, PyO3 and every Python-facing type
  are out of scope; `cargo build` in `/app` must not need them.
- **Readers for foreign file formats** — HDF5, netCDF, GeoTIFF, Kerchunk — and
  any virtual-dataset tooling built on them.
- **Wall-clock performance**, throughput, memory use, cache hit rates and the
  tuning of `MemCachingStorage`'s four sizes.
- **The physical key layout under a backend's prefix**, the encoding of
  snapshot, manifest and reference files, and the branch-version encoding
  (PROFILE.md P2, P3, P4, P6). No driver reads a stored object directly.
- **Attributes files.** `AttributesTable` carries no data;
  `Storage::fetch_attributes` and `write_attributes` are never called
  (PROFILE.md P7).
- **Reading an arbitrary historical branch version**, enumerating branch
  versions, and `Storage::ref_versions`' ordering.
- **Garbage collection, expiration and repository deletion** — no such
  operation exists at this milestone.
- **Conflict resolution and rebase.** A commit whose branch has moved simply
  fails with `Conflict`; nothing merges, replays or retries it for the caller.
- **`Snapshot`'s surface beyond `INITIAL_COMMIT_MESSAGE`**, `Manifest`'s
  surface, and the per-item accessors of `ChangeSet` beyond the five methods of
  4.4: they exist in the crate but no driver calls them.
- **Whether a `.` component in a path is rejected.** Only `..` is in scope
  (4.2).
- **`StoreError::BadKeyPrefix`, `NoSnapshot`, `PartialValuesPanic` and
  `Unknown`**, and the `RepositoryError` variants section 6 marks as not
  asserted.
