# TEST_INTERFACE.md — milestone 04 (the FlatBuffers metadata format)

This is the public contract the hidden verifier holds you to. Everything the
verifier names — crate identity, build command, module paths, exported symbols,
signatures, the on-disk byte layout, the error categories crossing the boundary,
and the crates it links against — appears **here** or in a section of
`/app/icechunk-spec/` named from `instruction.md`'s reading list. The verifier
may combine these in ways this document does not enumerate; it will not use an
interface this document does not name.

This file is self-contained. It restates the inherited contract *as this release
spells it*, because several inherited signatures changed shape here. Where a
signature appears in both this document and a milestone-3 memory, **this
document governs**.

Section 10 lists what is **out of scope**. Read it: it names several things the
0.2.0 release notes advertise that are Python-binding changes with no Rust
surface, and asserting them is explicitly out of scope.

---

## 1. Crate identity and build contract

| Item | Value |
| --- | --- |
| Workspace manifest | `/app/Cargo.toml` — `[workspace]`, `members = ["icechunk", ...]`, `default-members = ["icechunk"]`, `resolver = "2"` |
| Library package | `icechunk`, at `/app/icechunk`, manifest `/app/icechunk/Cargo.toml` |
| Package version | `0.2.0` |
| Crate root | `/app/icechunk/src/lib.rs` |
| Crate type | Rust library (`lib`), edition `2021` |
| Lockfile | `/app/Cargo.lock` — present, committed, consistent with the manifests |
| Build command | `cargo build` run in `/app` |
| Toolchain | the `rustc`/`cargo` in the verifier image (1.98); no nightly features |

The verifier runs `cargo build` in `/app` first. If that fails, **every** requirement
group of this milestone fails. It then creates a **throw-away driver crate
outside `/app`** that declares `icechunk = { path = "/app/icechunk" }` and
compiles its own integration tests against your crate. Your manifests are never
modified, and no test of yours is ever run or read as evidence.

`default-members = ["icechunk"]` is what makes `cargo build` at the workspace
root build the library and nothing else. Keep it, keep the member path, and keep
the package name: the drivers link `icechunk = { path = "/app/icechunk" }`.

The verifier image carries a pre-populated Cargo registry cache for this
release's shipped dependency graph plus the driver crate's own. A dependency you
*add* is fetched at check time, so prefer the crates already resolved in
`/app/Cargo.lock`.

**Version is not observable behaviour.** No requirement group asserts a version
string, a client name, or a `CARGO_PKG_VERSION`.

## 2. Workspace layout

```text
/app/
  Cargo.toml             workspace root       (required; default-members = ["icechunk"])
  Cargo.lock             resolved graph       (required)
  icechunk/
    Cargo.toml           the library package  (required, name = "icechunk")
    src/                 your implementation  (internal layout is yours)
  icechunk-spec/         the normative corpus (read-only; never edit a file in it)
  TEST_INTERFACE.md      this file
  WORKSPACE_HANDOFF.md   what to preserve when this milestone ends
  timer.sh               advisory countdown for this milestone's budget
```

Other workspace members may exist and are neither built nor checked.

Internal module organisation under `icechunk/src/` is yours **except** for the
module paths section 4 names: those are public API and the drivers spell them
verbatim (`icechunk::format::snapshot::Snapshot`, and so on).

## 3. Crates the verifier links against

The driver crate declares exactly these dependencies and no others:

```toml
icechunk    = { path = "/app/icechunk" }
bytes       = { version = "1", features = ["serde"] }
chrono      = { version = "0.4", features = ["serde"] }
futures     = "0.3"
serde_json  = "1"
tempfile    = "3"
tokio       = { version = "1", features = ["rt-multi-thread", "macros", "fs", "sync", "time", "io-util"] }
```

Consequences you must design for:

- `bytes::Bytes`, `chrono::DateTime<chrono::Utc>`, `serde_json::Value`,
  `futures::Stream`, `tokio::io::AsyncRead` and `tokio::sync::RwLock` cross the
  API boundary, so your crate must depend on **semver-compatible** versions of
  those crates. A different major version makes the types distinct and the
  drivers will not compile.
- The `flatbuffers` runtime crate is **not** a driver dependency. The drivers
  reach FlatBuffers-generated types only through `icechunk`'s own re-export
  (section 4.8) and through the plain-struct constructors and accessors it
  generates; they never name `flatbuffers::` themselves. You are free to pin any
  `flatbuffers` version whose generated code has the shapes section 4.8 names —
  the `25.x` line does; an older major generates different constructors and
  accessors and will not satisfy section 4.8.
- `zstd` is likewise not a driver dependency. The drivers observe compression
  only through the bytes in object storage and through your own reader.
- Async entry points are driven by `tokio`. Any `async fn` this document names
  must run on a plain tokio runtime, single- or multi-threaded.
- The drivers use `tempfile::TempDir` for scratch directories and address files
  inside them through `new_local_filesystem_storage` and `file://` URLs, so both
  must accept plain absolute paths such as `/tmp/xyz/data.bin`.
- The drivers unwrap `Result` values and format failures with `{:?}`, so every
  error type must implement `std::fmt::Debug` and `std::fmt::Display`. Where
  section 4 annotates a type with standard traits, those impls are required too.
  Section 10 excludes the **text** `Debug` and `Display` produce; it never
  excludes the impls themselves.
- The drivers also assert failures with `Result::unwrap_err`, which panics by
  formatting the **successful** value with `{:?}`. That bounds the `Ok` type,
  not the error type. Every `Ok` type reached that way at this milestone is
  `()`, a standard-library or dependency type, or one section 4 already
  annotates — except `repository::Repository` (section 4.10) and
  `session::Session` (section 4.11), which this document otherwise describes
  only through their `impl` blocks. **Both must implement `std::fmt::Debug`.**

## 4. Public API required at this milestone

### 4.1 Crate root — `icechunk/src/lib.rs`

Public modules, exactly this set:

```rust
pub mod asset_manager;
pub mod change_set;
pub mod config;
pub mod conflicts;
pub mod error;          // NEW at this milestone
pub mod format;
pub mod ops;
pub mod refs;
pub mod repository;
pub mod session;
pub mod storage;
pub mod store;
pub mod virtual_chunks;
```

`metadata` is **removed** at this milestone (section 7.1). `strategies` is
`#[cfg(test)]` upstream and is not reachable from an external test in any
release; the drivers never name it.

Crate-root re-exports, unchanged from milestone 3:

```rust
pub use config::{ObjectStoreConfig, RepositoryConfig};
pub use repository::Repository;
pub use storage::{
    new_in_memory_storage, new_local_filesystem_storage, new_s3_storage, ObjectStorage,
    Storage, StorageError,
};
pub use store::Store;
```

Optional, behind a cargo feature, out of scope:

```rust
#[cfg(feature = "logs")]
pub fn initialize_tracing();
```

### 4.2 `icechunk::error` — the `ICError` split (NEW)

Every error enum in the crate is split into a **kind** enum plus a type alias
over one generic carrier:

```rust
pub struct ICError<E> {
    pub kind: E,
    pub context: tracing_error::SpanTrace,
}

impl<E> ICError<E> {
    pub fn new(kind: E) -> Self;
    pub fn with_context(kind: E, context: SpanTrace) -> Self;
    pub fn kind(&self) -> &E;
    pub fn span(&self) -> &SpanTrace;
}

impl<E: std::fmt::Display> std::fmt::Display for ICError<E>;
impl<E: std::error::Error + 'static> std::error::Error for ICError<E>;   // source() == Some(&self.kind)
```

The `kind` field is **public**, and so is the `kind()` accessor: the drivers use
both, and they use the struct pattern

```rust
matches!(err, RepositoryError { kind: RepositoryErrorKind::RepositoryDoesntExist, .. })
```

`context` must exist as a public field and `span()` must be callable. Its
*contents* are never asserted.

The seven aliases, all required:

```rust
pub type IcechunkFormatError   = ICError<format::IcechunkFormatErrorKind>;
pub type RepositoryError       = ICError<repository::RepositoryErrorKind>;
pub type SessionError          = ICError<session::SessionErrorKind>;
pub type StoreError            = ICError<store::StoreErrorKind>;
pub type StorageError          = ICError<storage::StorageErrorKind>;
pub type RefError              = ICError<refs::RefErrorKind>;
pub type VirtualReferenceError = ICError<format::manifest::VirtualReferenceErrorKind>;
```

**Each alias is declared in the module that owns its kind** — `format`,
`repository`, `session`, `store`, `storage`, `refs` and `format::manifest`
respectively — and that is the path the drivers import it by
(`icechunk::store::StoreError`, `icechunk::refs::RefError`, and so on). Only
`ICError` itself lives in `icechunk::error`.

`From<K> for ICError<K>` must exist for each kind (the drivers write
`IcechunkFormatError::from(IcechunkFormatErrorKind::InvalidMagicNumbers)`), and
wrapper variants inside one kind carry the **other kind**, not the other alias —
see the variant lists below.

### 4.3 `icechunk::format` — ids, paths, ranges, constants

```rust
#[derive(Clone, PartialEq, Eq, PartialOrd, Ord, Hash)]   // plus `Debug`; its text is out of scope
pub struct ObjectId<const SIZE: usize, T: FileTypeTag>(pub [u8; SIZE], PhantomData<T>);
impl<const SIZE: usize, T: FileTypeTag> ObjectId<SIZE, T> {
    pub fn random() -> Self;
    pub fn new(buf: [u8; SIZE]) -> Self;
    pub const FAKE: Self;   // the all-zero id — `FAKE.0 == [0u8; SIZE]`
}
// Display / From<&ObjectId> for String is Base 32 Crockford; TryFrom<&str> and
// TryFrom<&[u8]> parse it back. A 12-byte id prints as 20 characters.

pub type SnapshotId   = ObjectId<12, SnapshotTag>;
pub type ManifestId   = ObjectId<12, ManifestTag>;
pub type ChunkId      = ObjectId<12, ChunkTag>;
pub type AttributesId = ObjectId<12, AttributesTag>;
pub type NodeId       = ObjectId<8,  NodeTag>;

#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct ChunkIndices(pub Vec<u32>);
pub type ChunkOffset = u64;
pub type ChunkLength = u64;

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Path(/* private */);
impl Display for Path;                    // the path text, e.g. "/" or "/array"
impl Path {
    pub fn root() -> Path;
    pub fn new(path: &str) -> Result<Path, PathError>;
    pub fn starts_with(&self, other: &Path) -> bool;
    pub fn ancestors(&self) -> impl Iterator<Item = Path> + '_;
    pub fn name(&self) -> Option<&str>;
}
impl TryFrom<&str> for Path { type Error = PathError; }     // and &String, String
#[derive(Debug, Clone, PartialEq, Eq)] #[non_exhaustive]
pub enum PathError { NotAbsolute, NotCanonic }

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum ByteRange { Bounded(Range<ChunkOffset>), From(ChunkOffset),
                     Last(ChunkLength), Until(ChunkOffset) }
impl ByteRange { pub const ALL: Self; /* = From(0) */ }
```

`Path::new` accepts only absolute, canonic paths, and `TryFrom<&str>` is the same
function: a path that does not start with `/` is `PathError::NotAbsolute`, and
one that is not **already** in normalised form — one that carries a `.` or `..`
component, or a redundant separator, as written — is `PathError::NotCanonic`.
Normalisation is never performed on the caller's behalf, so `/a/../b` is
`NotCanonic` rather than accepted as `/b`.

`format_constants` — the file-header vocabulary, unchanged from milestone 3 and
required in exactly this form:

```rust
pub mod format_constants {
    #[repr(u8)] #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub enum FileTypeBin { Snapshot = 1, Manifest = 2, Attributes = 3,
                           TransactionLog = 4, Chunk = 5 }

    #[repr(u8)] #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub enum SpecVersionBin { V0dot1 = 1 }
    impl SpecVersionBin { pub fn current() -> Self; }        // == V0dot1 at this release

    #[repr(u8)] #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub enum CompressionAlgorithmBin { None = 0, Zstd = 1 }

    // TryFrom<u8> for all three; Error = String; an unknown code is an Err.
    pub const ICECHUNK_FORMAT_MAGIC_BYTES: &[u8];  // "ICE🧊CHUNK" UTF-8, 12 bytes
    pub const LATEST_ICECHUNK_FORMAT_VERSION_METADATA_KEY: &str;
    pub const ICECHUNK_LIB_VERSION: &str;
    pub static ICECHUNK_CLIENT_NAME: LazyLock<String>;
    pub const ICECHUNK_CLIENT_NAME_METADATA_KEY: &str;
    pub const ICECHUNK_FILE_TYPE_SNAPSHOT: &str;
    pub const ICECHUNK_FILE_TYPE_MANIFEST: &str;
    pub const ICECHUNK_FILE_TYPE_TRANSACTION_LOG: &str;
    pub const ICECHUNK_FILE_TYPE_METADATA_KEY: &str;
    pub const ICECHUNK_COMPRESSION_METADATA_KEY: &str;
    pub const ICECHUNK_COMPRESSION_ZSTD: &str;
}
```

The drivers assert the **numeric codes**, the `TryFrom<u8>` round trips and
their rejections, and the magic-byte value and length. They do not assert the
string constants' values.

`IcechunkFormatErrorKind` — `#[non_exhaustive]`, and the drivers match these
variants by name:

```rust
pub enum IcechunkFormatErrorKind {
    VirtualReferenceError(VirtualReferenceErrorKind),
    NodeNotFound { path: Path },
    ChunkCoordinatesNotFound { coords: ChunkIndices },
    ManifestInfoNotFound { manifest_id: ManifestId },
    InvalidMagicNumbers,
    InvalidSpecVersion,
    InvalidFileType { expected: FileTypeBin, got: u8 },
    InvalidCompressionAlgorithm,
    InvalidFlatBuffer(flatbuffers::InvalidFlatbuffer),   // NEW at this milestone
    DeserializationError(rmp_serde::decode::Error),
    SerializationError(rmp_serde::encode::Error),
    IO(std::io::Error),
    Path(PathError),
    InvalidTimestamp,                                    // NEW at this milestone
}
pub type IcechunkFormatError = ICError<IcechunkFormatErrorKind>;
pub type IcechunkResult<T> = Result<T, IcechunkFormatError>;
```

`InvalidFlatBuffer` wraps whatever verification error your FlatBuffers runtime
produces; the drivers match it with a wildcard (`InvalidFlatBuffer(_)`) and never
inspect the inner value. `InvalidFileType`'s two fields are matched by name and
value.

### 4.4 `format::snapshot`

```rust
#[derive(Debug, PartialEq)]
pub struct Snapshot { /* private: one owned byte buffer */ }

impl Snapshot {
    pub const INITIAL_COMMIT_MESSAGE: &'static str = "Repository initialized";

    pub fn from_buffer(buffer: Vec<u8>) -> IcechunkResult<Snapshot>;  // VERIFIES, see §5.3
    pub fn bytes(&self) -> &[u8];
    pub fn from_iter<E, I>(
        id: Option<SnapshotId>,
        parent_id: Option<SnapshotId>,
        message: String,
        properties: Option<SnapshotProperties>,
        manifest_files: Vec<ManifestFileInfo>,  // ANY order; stored sorted, see below
        sorted_iter: I,                       // items MUST already be sorted by `path`
    ) -> IcechunkResult<Self>
    where IcechunkFormatError: From<E>, I: IntoIterator<Item = Result<NodeSnapshot, E>>;
    pub fn initial() -> IcechunkResult<Self>;
    pub fn id(&self) -> SnapshotId;                                  // by value
    pub fn parent_id(&self) -> Option<SnapshotId>;                   // by value
    pub fn metadata(&self) -> IcechunkResult<SnapshotProperties>;    // fallible
    pub fn flushed_at(&self) -> IcechunkResult<DateTime<Utc>>;       // fallible
    pub fn message(&self) -> String;                                 // by value
    pub fn get_manifest_file(&self, id: &ManifestId) -> Option<ManifestFileInfo>;
    pub fn manifest_info(&self, id: &ManifestId) -> Option<ManifestFileInfo>;
    pub fn manifest_files(&self) -> impl Iterator<Item = ManifestFileInfo> + '_;
    pub fn adopt(&self, new_child: &Snapshot) -> IcechunkResult<Self>;
    pub fn get_node(&self, path: &Path) -> IcechunkResult<NodeSnapshot>;
    pub fn iter(&self) -> impl Iterator<Item = IcechunkResult<NodeSnapshot>> + '_;
    pub fn iter_arc(self: Arc<Self>) -> impl Iterator<Item = IcechunkResult<NodeSnapshot>>;
    pub fn len(&self) -> usize;                                      // number of nodes
    pub fn is_empty(&self) -> bool;
}
```

Required behaviour the drivers observe:

- `Snapshot::initial()` has no parent, zero nodes, the message
  `INITIAL_COMMIT_MESSAGE`, and metadata containing the key `"__root"`.
- The sorted-input precondition on `from_iter` applies to `sorted_iter` **only**.
  `manifest_files` may be supplied in **any** order: `from_iter` sorts it and
  stores it **ascending by `ManifestFileInfo.id`**, which is the buffer
  invariant section 6 requires and the order `manifest_files()` yields it back
  in. A caller handing it three descending ids reads them back ascending.
- `iter()` and `iter_arc()` yield nodes **in ascending `path` order**, which is
  the order `get_node`'s lookup depends on.
- `get_node` for an absent path is `IcechunkFormatErrorKind::NodeNotFound`.
- `parent.adopt(&child)` returns a snapshot with **`child`'s** id, message,
  metadata, manifest files and nodes, and **`parent`'s** id as its parent. The
  receiver is the parent and the argument is the child.
- `Snapshot` is `PartialEq`, and two snapshots are equal exactly when their
  `bytes()` are equal.
- `flushed_at()` and `metadata()` are fallible because the buffer stores raw
  values the accessor has to interpret. `snapshot.fbs` stores the instant as a
  `uint64` of microseconds since the Unix epoch, so a stored value outside the
  range `chrono::DateTime<Utc>` can represent is
  `IcechunkFormatErrorKind::InvalidTimestamp` — an error, never a panic and
  never a silently wrong instant.

```rust
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SnapshotInfo {
    pub id: SnapshotId,
    pub parent_id: Option<SnapshotId>,
    pub flushed_at: DateTime<chrono::Utc>,
    pub message: String,
    pub metadata: SnapshotProperties,
}
impl TryFrom<&Snapshot> for SnapshotInfo { type Error = IcechunkFormatError; }
impl SnapshotInfo { pub fn is_initial(&self) -> bool; }   // parent_id.is_none()

pub type SnapshotProperties = BTreeMap<String, serde_json::Value>;   // sorted iteration

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct NodeSnapshot {
    pub id: NodeId,
    pub path: Path,
    pub user_data: bytes::Bytes,
    pub node_data: NodeData,
}
impl NodeSnapshot { pub fn node_type(&self) -> NodeType; }

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum NodeData {
    Array {
        shape: ArrayShape,
        dimension_names: Option<Vec<DimensionName>>,
        manifests: Vec<ManifestRef>,
    },
    Group,
}

#[derive(Clone, Debug, Hash, PartialEq, Eq)]
pub enum NodeType { Group, Array }

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct DimensionShape { /* private fields */ }
impl DimensionShape {
    pub fn new(array_length: u64, chunk_length: std::num::NonZeroU64) -> Self;
    pub fn array_length(&self) -> u64;
    pub fn chunk_length(&self) -> u64;
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ArrayShape(/* private */);
impl ArrayShape {
    pub fn new<I>(it: I) -> Option<Self> where I: IntoIterator<Item = (u64, u64)>;
    pub fn valid_chunk_coord(&self, coord: &ChunkIndices) -> bool;
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum DimensionName { NotSpecified, Name(String) }
impl From<Option<&str>> for DimensionName;   // None -> NotSpecified
impl From<&str> for DimensionName;           // ""   -> NotSpecified

#[derive(Debug, PartialEq, Serialize, Deserialize, Clone, Eq, Hash)]
pub struct ManifestFileInfo { pub id: ManifestId, pub size_bytes: u64, pub num_chunk_refs: u32 }
impl ManifestFileInfo { pub fn new(manifest: &Manifest, size_bytes: u64) -> Self; }
```

`ArrayShape::new` returns `None` if **any** chunk length is zero — the
per-dimension chunk length is a `NonZeroU64`. `valid_chunk_coord` is `false` for
a coordinate outside the chunk grid; `Session::set_chunk_ref` turns that into
`SessionErrorKind::InvalidIndex`.

Bridges from the generated types (all public, all used by the drivers):

```rust
impl From<&gen::ObjectId8>  for NodeId;
impl From<&gen::ObjectId12> for ManifestId;
impl From<&gen::ObjectId12> for AttributesId;
impl From<&gen::DimensionShape> for DimensionShape;
impl From<&gen::ManifestFileInfo> for ManifestFileInfo;
impl<'a> From<gen::ManifestRef<'a>>    for ManifestRef;
impl<'a> From<gen::ArrayNodeData<'a>>  for NodeData;
impl<'a> From<gen::GroupNodeData<'a>>  for NodeData;
impl<'a> TryFrom<gen::NodeSnapshot<'a>> for NodeSnapshot;
impl<'a> From<gen::ChunkIndices<'a>>   for ChunkIndices;   // in format/mod.rs
```

### 4.5 `format::manifest`

```rust
#[derive(Debug)]
pub struct Manifest { /* private: one owned byte buffer */ }   // NOT PartialEq, no Default

impl Manifest {
    pub fn id(&self) -> ManifestId;                       // a METHOD, by value
    pub fn bytes(&self) -> &[u8];
    pub fn from_buffer(buffer: Vec<u8>) -> Result<Manifest, IcechunkFormatError>; // VERIFIES
    pub async fn from_stream<E>(stream: impl Stream<Item = Result<ChunkInfo, E>>)
        -> Result<Option<Self>, E>;                       // None for an empty manifest
    pub async fn from_iter<T: IntoIterator<Item = ChunkInfo>>(iter: T)
        -> Result<Option<Self>, std::convert::Infallible>;
    pub fn len(&self) -> usize;                           // number of chunk refs
    pub fn is_empty(&self) -> bool;
    pub fn get_chunk_payload(&self, node: &NodeId, coord: &ChunkIndices)
        -> IcechunkResult<ChunkPayload>;                  // by value
    pub fn iter(self: Arc<Self>, node: NodeId)
        -> impl Iterator<Item = Result<(ChunkIndices, ChunkPayload), IcechunkFormatError>>;
    pub fn chunk_payloads(&self)
        -> impl Iterator<Item = Result<ChunkPayload, IcechunkFormatError>> + '_;
}
```

Required behaviour:

- `from_iter`/`from_stream` over an empty input yields `Ok(None)`; there is no
  empty-manifest object.
- `from_iter` accepts input in any order and stores it sorted by
  `(node, coord)`; `iter(node)` yields that array's refs in ascending coordinate
  order, and an unknown node yields nothing.
- `get_chunk_payload` for an absent coordinate is
  `IcechunkFormatErrorKind::ChunkCoordinatesNotFound`.
- A manifest holds exactly three kinds of chunk reference, and each survives a
  `bytes()` → `from_buffer` round trip unchanged.

**One manifest per array at this release.** A commit writes **one** manifest
object for each array whose chunk references it creates or changes, holding all
of that array's references; an array with no chunk changes keeps the manifest it
already had, and an array with no chunk references at all gets no manifest
(`from_iter`/`from_stream` yield `None`). Two consequences the drivers observe
directly: a decoded `NodeData::Array` for an array that has chunk references
carries exactly **one** `ManifestRef` — an array with none carries an empty list
— and a snapshot's `manifest_files()` has exactly one entry per array that has
chunks.

`spec.md` "Chunk Manifest Files" also permits a manifest to hold several
arrays' references and a single array's chunks to be spread across several
manifests. Neither happens at this release: splitting a manifest is
`design/005-manifest-split.md`, which `icechunk-spec/README.md` marks as a later
milestone, and nothing here merges two arrays into one manifest.

```rust
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ManifestExtents(/* private */);
impl ManifestExtents {
    pub fn new(from: &[u32], to: &[u32]) -> Self;         // one Range per dimension
    pub fn iter(&self) -> impl Iterator<Item = &std::ops::Range<u32>>;
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ManifestRef { pub object_id: ManifestId, pub extents: ManifestExtents }

#[derive(Clone, Debug, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub struct ChunkInfo { pub node: NodeId, pub coord: ChunkIndices, pub payload: ChunkPayload }

#[non_exhaustive]
#[derive(Clone, Debug, Hash, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
pub enum ChunkPayload { Inline(Bytes), Virtual(VirtualChunkRef), Ref(ChunkRef) }

// each of the five types below carries this same derive set
#[derive(Clone, Debug, Hash, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
pub struct ChunkRef { pub id: ChunkId, pub offset: ChunkOffset, pub length: ChunkLength }
pub struct VirtualChunkRef { pub location: VirtualChunkLocation, pub offset: ChunkOffset,
                             pub length: ChunkLength, pub checksum: Option<Checksum> }
pub enum Checksum { LastModified(SecondsSinceEpoch), ETag(storage::ETag) }
pub struct SecondsSinceEpoch(pub u32);
pub struct VirtualChunkLocation(pub String);
impl VirtualChunkLocation {
    pub fn from_absolute_path(path: &str) -> Result<VirtualChunkLocation, VirtualReferenceError>;
}

#[derive(Debug, thiserror::Error)] #[non_exhaustive]
pub enum VirtualReferenceErrorKind {
    NoContainerForUrl(String), CannotParseUrl(url::ParseError), InvalidCredentials(String),
    NoPathSegments(String), UnsupportedScheme(String), CannotParseBucketName(String),
    FetchError(..), ObjectModified(String),
    InvalidObjectSize { expected: u64, available: u64 }, OtherError(..),
}
pub type VirtualReferenceError = ICError<VirtualReferenceErrorKind>;
```

`VirtualChunkLocation::from_absolute_path` normalises to
`"{scheme}://{host}/{path}"`, with an empty host for `file:`. The path part is
**rebuilt from the URL's non-empty path segments**, joined by `/`, rather than
copied verbatim, so a redundant separator is collapsed: `file:///tmp//a//b.nc`
normalises to `file:///tmp/a/b.nc`. The drivers build `file:///…` locations from
a `tempfile::TempDir` and read the referenced bytes back through `Store::get`,
so a `file://` virtual chunk must resolve offline.

It rejects **three** shapes by name: a string that is not a parseable URL is
`VirtualReferenceErrorKind::CannotParseUrl(_)`; a URL that carries no path
segments — a scheme with no authority, such as `weird:x/y` — is
`NoPathSegments(_)`; and a URL whose scheme is not `file` and which carries no
host — such as `weird:///y` — is `CannotParseBucketName(_)`, because there is no
bucket to name. It does **not** restrict the scheme: an unknown scheme **with a
host**, such as `weird://x/y`, is normalised and returned, and it is the virtual
chunk *container* lookup (section 4.14) that later declines to handle it.

`UnsupportedScheme` is declared in the enum above but **nothing in this release
constructs it**: it is dead at this milestone, so the enumeration of rejections
in the paragraph above is the complete set `from_absolute_path` can produce. The
remaining variants — `InvalidCredentials`, `FetchError`, `ObjectModified`,
`InvalidObjectSize` and `OtherError` — are reachable, but only on the remote
object-store fetch paths that section 10 keeps out of scope; no requirement group
reaches them.

### 4.6 `format::transaction_log`

```rust
#[derive(Clone, Debug, PartialEq, Default)]
pub struct TransactionLog { /* private: one owned byte buffer */ }

impl TransactionLog {
    pub fn new(id: &SnapshotId, cs: &ChangeSet) -> Self;
    pub fn from_buffer(buffer: Vec<u8>) -> IcechunkResult<TransactionLog>;  // VERIFIES
    pub fn bytes(&self) -> &[u8];
    pub fn new_groups(&self)     -> impl Iterator<Item = NodeId> + '_;
    pub fn new_arrays(&self)     -> impl Iterator<Item = NodeId> + '_;
    pub fn deleted_groups(&self) -> impl Iterator<Item = NodeId> + '_;
    pub fn deleted_arrays(&self) -> impl Iterator<Item = NodeId> + '_;
    pub fn updated_groups(&self) -> impl Iterator<Item = NodeId> + '_;
    pub fn updated_arrays(&self) -> impl Iterator<Item = NodeId> + '_;
    pub fn updated_chunks(&self)
        -> impl Iterator<Item = (NodeId, impl Iterator<Item = ChunkIndices> + '_)> + '_;
    pub fn updated_chunks_for(&self, node: &NodeId) -> impl Iterator<Item = ChunkIndices> + '_;
    pub fn group_created(&self, id: &NodeId) -> bool;
    pub fn array_created(&self, id: &NodeId) -> bool;
    pub fn group_deleted(&self, id: &NodeId) -> bool;
    pub fn array_deleted(&self, id: &NodeId) -> bool;
    pub fn group_updated(&self, id: &NodeId) -> bool;
    pub fn array_updated(&self, id: &NodeId) -> bool;
    pub fn chunks_updated(&self, id: &NodeId) -> bool;
    pub fn len(&self) -> usize;
    pub fn is_empty(&self) -> bool;
}
```

`len()` counts every change the transaction records: created groups, created
arrays, deleted groups, deleted arrays, updated groups, updated arrays, and each
updated chunk coordinate. A commit that creates four nodes has `len() == 4`; a
commit that writes one chunk, updates one group and one array and deletes one
array also has `len() == 4`.

`Default` exists (the derive is part of the type's declared traits), but a
default-constructed value carries no verified buffer and the drivers never call
an accessor on one.

```rust
#[derive(Debug, Default)] pub struct DiffBuilder;
impl DiffBuilder {
    pub fn add_changes(&mut self, tx: &TransactionLog);
    pub async fn to_diff(self, from: &Session, to: &Session) -> SessionResult<Diff>;
}

#[derive(Clone, Debug, PartialEq)]
pub struct Diff {
    pub new_groups: BTreeSet<Path>,      pub new_arrays: BTreeSet<Path>,
    pub deleted_groups: BTreeSet<Path>,  pub deleted_arrays: BTreeSet<Path>,
    pub updated_groups: BTreeSet<Path>,  pub updated_arrays: BTreeSet<Path>,
    pub updated_chunks: BTreeMap<Path, BTreeSet<ChunkIndices>>,
}
impl Diff { pub fn is_empty(&self) -> bool; }
```

### 4.7 `format::serializers`

The `current` submodule is **gone**. Six free functions remain, and they only
move bytes:

```rust
pub fn serialize_snapshot(snapshot: &Snapshot, version: SpecVersionBin,
                          write: &mut impl Write) -> Result<(), std::io::Error>;
pub fn serialize_manifest(manifest: &Manifest, version: SpecVersionBin,
                          write: &mut impl Write) -> Result<(), std::io::Error>;
pub fn serialize_transaction_log(tx: &TransactionLog, version: SpecVersionBin,
                                 write: &mut impl Write) -> Result<(), std::io::Error>;

pub fn deserialize_snapshot(version: SpecVersionBin, read: Box<dyn Read>)
    -> Result<Snapshot, IcechunkFormatError>;
pub fn deserialize_manifest(version: SpecVersionBin, read: Box<dyn Read>)
    -> Result<Manifest, IcechunkFormatError>;
pub fn deserialize_transaction_log(version: SpecVersionBin, read: Box<dyn Read>)
    -> Result<TransactionLog, IcechunkFormatError>;
```

Required behaviour, and the drivers assert it directly: `serialize_X` writes
**exactly** `x.bytes()` and nothing else — no length prefix, no framing, no
Icechunk header; and `deserialize_X` reads the stream to its end and hands the
buffer to `X::from_buffer`, so **verification happens inside `deserialize_X`**,
before any accessor can be called.

### 4.8 `format::flatbuffers` — the generated module (NEW)

```rust
pub mod flatbuffers {           // icechunk::format::flatbuffers
    pub mod gen { /* the module described by icechunk-spec/flatbuffers/all.fbs; see section 6 */ }
}
```

Reachable as `icechunk::format::flatbuffers::gen::*`. The drivers use only the
parts that need no FlatBuffers runtime type in the signature:

```rust
pub struct ObjectId12(pub [u8; 12]);
impl ObjectId12 { pub fn new(bytes: &[u8; 12]) -> Self; }
impl Default for ObjectId12;        // default() is the all-zero id, [0u8; 12]

pub struct ObjectId8(pub [u8; 8]);
impl ObjectId8 { pub fn new(bytes: &[u8; 8]) -> Self; }
impl Default for ObjectId8;         // default() is the all-zero id, [0u8; 8]

pub struct DimensionShape(/* fixed-size struct */);
impl DimensionShape {
    pub fn new(array_length: u64, chunk_length: u64) -> Self;
    pub fn array_length(&self) -> u64;
    pub fn chunk_length(&self) -> u64;
}

pub struct ChunkIndexRange(/* fixed-size struct */);
impl ChunkIndexRange {
    pub fn new(from: u32, to: u32) -> Self;
    pub fn from(&self) -> u32;      // inclusive
    pub fn to(&self) -> u32;        // exclusive
}

pub struct ManifestFileInfo(/* fixed-size struct */);
impl ManifestFileInfo {
    pub fn new(id: &ObjectId12, size_bytes: u64, num_chunk_refs: u32) -> Self;
    pub fn id(&self) -> &ObjectId12;
    pub fn size_bytes(&self) -> u64;
    pub fn num_chunk_refs(&self) -> u32;
}

pub struct NodeData(pub u8);        // the union tag
impl NodeData {
    pub const NONE:  Self;          // 0
    pub const Array: Self;          // 1
    pub const Group: Self;          // 2
}
```

The tuple fields of `ObjectId8` and `ObjectId12` are public, and their values are
the raw id bytes: `NodeId::from(&gen::ObjectId8::new(&b)) == NodeId::new(b)`.
The union tag values follow declaration order in `snapshot.fbs` (`Array` first,
then `Group`).

`DimensionShape`, `ChunkIndexRange` and `ManifestFileInfo` are **`struct`s** in
`snapshot.fbs`, not tables: a struct's representation is an opaque fixed-size
byte array, so the constructor and the accessors above are the only way to build
one or read one back, and the drivers use exactly those. Each `new` takes the
schema's fields in the order the schema declares them — for `ManifestFileInfo`,
`id`, `size_bytes`, `num_chunk_refs`. Because they are structs, section 4.4's
bridges out of them take a **reference** (`From<&gen::ManifestFileInfo>`,
`From<&gen::DimensionShape>`) rather than an owned table with a lifetime.

The remaining generated table types — `Snapshot`, `Manifest`, `TransactionLog`,
`NodeSnapshot`, `ChunkRef`, `ArrayManifest`, `MetadataItem`, `ManifestRef`,
`DimensionName`, `ArrayNodeData`, `GroupNodeData`, `ChunkIndices`,
`ArrayUpdatedChunks` — must exist under `gen` because the bridges in section 4.4
name them in their signatures, but the drivers do not construct or read them
directly. No `file_identifier` is declared in any schema, so **no**
`root_as_*` helper and no `*_IDENTIFIER` constant is generated or required.

### 4.9 `icechunk::asset_manager`

```rust
#[derive(Debug, Serialize, Deserialize)]
pub struct AssetManager { /* private */ }

impl AssetManager {
    pub fn new(storage: Arc<dyn Storage + Send + Sync>, storage_settings: storage::Settings,
               num_snapshot_nodes: u64, num_chunk_refs: u64, num_transaction_changes: u64,
               num_bytes_attributes: u64, num_bytes_chunks: u64,
               compression_level: u8) -> Self;
    pub fn new_no_cache(storage: Arc<dyn Storage + Send + Sync>,
                        storage_settings: storage::Settings, compression_level: u8) -> Self;
    pub fn new_with_config(storage: Arc<dyn Storage + Send + Sync>,
                           storage_settings: storage::Settings,
                           config: &config::CachingConfig, compression_level: u8) -> Self;

    pub async fn write_snapshot(&self, snapshot: Arc<Snapshot>) -> RepositoryResult<()>;
    pub async fn fetch_snapshot(&self, id: &SnapshotId) -> RepositoryResult<Arc<Snapshot>>;
    pub async fn write_manifest(&self, manifest: Arc<Manifest>) -> RepositoryResult<u64>;
    pub async fn fetch_manifest(&self, id: &ManifestId, size: u64)
        -> RepositoryResult<Arc<Manifest>>;
    pub async fn fetch_manifest_unknown_size(&self, id: &ManifestId)
        -> RepositoryResult<Arc<Manifest>>;
    pub async fn write_transaction_log(&self, transaction_id: SnapshotId,
                                       log: Arc<TransactionLog>) -> RepositoryResult<()>;
    pub async fn fetch_transaction_log(&self, transaction_id: &SnapshotId)
        -> RepositoryResult<Arc<TransactionLog>>;
    pub async fn write_chunk(&self, chunk_id: ChunkId, bytes: Bytes) -> RepositoryResult<()>;
    pub async fn fetch_chunk(&self, chunk_id: &ChunkId, range: &Range<ChunkOffset>)
        -> RepositoryResult<Bytes>;
    pub async fn snapshot_ancestry(self: Arc<Self>, snapshot_id: &SnapshotId)
        -> RepositoryResult<impl Stream<Item = RepositoryResult<SnapshotInfo>>>;
    pub async fn get_snapshot_last_modified(&self, snapshot_id: &SnapshotId)
        -> RepositoryResult<DateTime<Utc>>;
}
```

`write_manifest` returns the **byte length of the object it wrote**, which is
what `ManifestFileInfo::size_bytes` records and what `fetch_manifest` takes.

`get_snapshot_last_modified(id)` reports **when the object store accepted the
snapshot object** — the stored object's own last-modified metadata, read back
from storage. That is not the `flushed_at` instant the snapshot records inside
its own buffer, and the two are not required to agree; what is required is that
the reported instant lies between the moment the object was written and the
moment it is read back.

The asset manager is what applies and checks the on-disk envelope of section 5:
every `fetch_*` writes/reads the 39-byte header plus a compressed payload, and
every header failure surfaces as a `RepositoryErrorKind::FormatError(kind)`.
`fetch_*` does **not** require the id inside the decoded object to equal the id
requested; an object is self-describing and the drivers rely on that.

### 4.10 `icechunk::repository`

```rust
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)] #[non_exhaustive]
pub enum VersionInfo {
    #[serde(rename = "snapshot_id")] SnapshotId(SnapshotId),
    #[serde(rename = "tag")]         TagRef(String),
    #[serde(rename = "branch")]      BranchTipRef(String),
}

#[derive(Debug, thiserror::Error)] #[non_exhaustive]
pub enum RepositoryErrorKind {
    StorageError(StorageErrorKind),          // wrapper variants carry the KIND
    FormatError(IcechunkFormatErrorKind),
    Ref(RefErrorKind),
    SnapshotNotFound { id: SnapshotId },
    InvalidSnapshotId(String),
    Tag(String),
    ParentDirectoryNotClean,
    RepositoryDoesntExist,
    SerializationError(rmp_serde::encode::Error),
    DeserializationError(rmp_serde::decode::Error),
    ConflictingPathNotFound(NodeId),
    ConfigDeserializationError(serde_yaml_ng::Error),
    ConfigWasUpdated,
    Conflict { expected_parent: Option<SnapshotId>, actual_parent: Option<SnapshotId> },
    IOError(std::io::Error),
    ConcurrencyError(tokio::task::JoinError),
    CannotDeleteMain,
}
pub type RepositoryError = ICError<RepositoryErrorKind>;
pub type RepositoryResult<T> = Result<T, RepositoryError>;

impl Repository {
    pub async fn create(config: Option<RepositoryConfig>,
                        storage: Arc<dyn Storage + Send + Sync>,
                        virtual_chunk_credentials: HashMap<ContainerName, Credentials>)
        -> RepositoryResult<Self>;
    pub async fn open(/* same three arguments */) -> RepositoryResult<Self>;
    pub async fn open_or_create(/* same three arguments */) -> RepositoryResult<Self>;
    pub async fn exists(storage: &(dyn Storage + Send + Sync)) -> RepositoryResult<bool>;
    pub fn reopen(&self, config: Option<RepositoryConfig>,
                  virtual_chunk_credentials: Option<HashMap<ContainerName, Credentials>>)
        -> RepositoryResult<Self>;
    pub async fn fetch_config(storage: &(dyn Storage + Send + Sync))
        -> RepositoryResult<Option<(RepositoryConfig, storage::VersionInfo)>>;
    pub async fn save_config(&self) -> RepositoryResult<storage::VersionInfo>;
    pub fn config(&self) -> &RepositoryConfig;
    pub fn storage_settings(&self) -> &storage::Settings;
    pub fn storage(&self) -> &Arc<dyn Storage + Send + Sync>;
    pub fn asset_manager(&self) -> &Arc<AssetManager>;

    pub async fn ancestry(&self, version: &VersionInfo)
        -> RepositoryResult<impl Stream<Item = RepositoryResult<SnapshotInfo>> + '_>;
    pub async fn ancestry_arc(self: Arc<Self>, version: &VersionInfo)
        -> RepositoryResult<impl Stream<Item = RepositoryResult<SnapshotInfo>>>;
    pub async fn snapshot_ancestry(&self, snapshot_id: &SnapshotId)
        -> RepositoryResult<impl Stream<Item = RepositoryResult<SnapshotInfo>> + '_>;
    pub async fn snapshot_ancestry_arc(self: Arc<Self>, snapshot_id: &SnapshotId)
        -> RepositoryResult<impl Stream<Item = RepositoryResult<SnapshotInfo>>>;

    pub async fn create_branch(&self, branch: &str, snapshot_id: &SnapshotId)
        -> RepositoryResult<()>;
    pub async fn list_branches(&self) -> RepositoryResult<BTreeSet<String>>;
    pub async fn lookup_branch(&self, branch: &str) -> RepositoryResult<SnapshotId>;
    pub async fn reset_branch(&self, branch: &str, snapshot_id: &SnapshotId)
        -> RepositoryResult<()>;
    pub async fn delete_branch(&self, branch: &str) -> RepositoryResult<()>;
    pub async fn create_tag(&self, tag: &str, snapshot_id: &SnapshotId) -> RepositoryResult<()>;
    pub async fn list_tags(&self) -> RepositoryResult<BTreeSet<String>>;
    pub async fn lookup_tag(&self, tag: &str) -> RepositoryResult<SnapshotId>;
    pub async fn delete_tag(&self, tag: &str) -> RepositoryResult<()>;

    pub async fn diff(&self, from: &VersionInfo, to: &VersionInfo) -> SessionResult<Diff>;
    pub async fn readonly_session(&self, version: &VersionInfo) -> RepositoryResult<Session>;
    pub async fn writable_session(&self, branch: &str) -> RepositoryResult<Session>;
}
```

Required behaviour the drivers observe:

- `create` writes an initial snapshot and points `main` at it. After it,
  `exists` is `true`, `ancestry` on `main` yields exactly one `SnapshotInfo`, and
  that info `is_initial()`. It writes **no configuration object** unless it was
  handed a `config` that differs from `RepositoryConfig::default()`: after
  `create(None, …)`, `fetch_config` is `None` until `save_config` is called.
- `create` requires a **clean prefix**: if the storage root already holds any
  object at all, it fails with
  `RepositoryErrorKind::ParentDirectoryNotClean` and writes nothing. Creating a
  second repository over an existing one is therefore an error, not an
  overwrite.
- `exists` is decided **solely** by whether the default branch `main` resolves.
  See section 5.4 for what that means for a repository written before this
  release.
- `open` on storage where `exists` is `false` fails with
  `RepositoryErrorKind::RepositoryDoesntExist`.
- `delete_branch("main")` fails with `RepositoryErrorKind::CannotDeleteMain`.
- A tag name is used **once**: `create_tag` on a name that already exists, and
  `create_tag` on a name that was created and then deleted, both fail with
  `RepositoryErrorKind::Ref(RefErrorKind::TagAlreadyExists(_))`. Deleting a tag
  retires its name permanently — the name never becomes available again.
- Looking up a reference that does not exist — `lookup_tag`, `lookup_branch`, or
  a `readonly_session` on `VersionInfo::TagRef` / `BranchTipRef` — fails with
  `RepositoryErrorKind::Ref(RefErrorKind::RefNotFound(_))`.
- `readonly_session(&VersionInfo::SnapshotId(id))` for an `id` that has no
  snapshot object fails with `RepositoryErrorKind::SnapshotNotFound { id }`.
- `ancestry` is a lazy `futures::Stream`: a consumer may take a prefix and drop
  the rest, and the prefix it took must be complete and in newest-first order.
- `diff` returns `SessionResult<Diff>`, not `RepositoryResult<Diff>`. It is a
  `DiffBuilder` fold: `add_changes` is applied to the transaction log of every
  snapshot in `to`'s ancestry that comes after `from`, up to and including `to`
  (the repository's initial snapshot excepted — it carries no transaction log),
  and the accumulated builder is resolved with
  `to_diff(&from_session, &to_session)` against read-only sessions opened at the
  two versions. Driving a `DiffBuilder` by hand over the same transaction logs
  and resolving it against the same two sessions therefore yields an **equal**
  `Diff`. If `to`'s ancestry does not reach `from`, `diff` fails with
  `SessionErrorKind::BadSnapshotChainForDiff`.

### 4.11 `icechunk::session`

```rust
pub type SessionResult<T> = Result<T, SessionError>;
pub type SessionError = ICError<SessionErrorKind>;

#[derive(Debug, thiserror::Error)] #[non_exhaustive]
pub enum SessionErrorKind {
    RepositoryError(RepositoryErrorKind), StorageError(StorageErrorKind),
    FormatError(IcechunkFormatErrorKind), Ref(RefErrorKind),
    VirtualReferenceError(VirtualReferenceErrorKind),
    ReadOnlySession,
    SnapshotNotFound { id: SnapshotId },
    AncestorNodeNotFound { prefix: Path },
    NodeNotFound { path: Path, message: String },
    NotAnArray { node: NodeSnapshot, message: String },
    NotAGroup { node: NodeSnapshot, message: String },
    AlreadyExists { node: NodeSnapshot, message: String },
    NoChangesToCommit,
    InvalidSnapshotTimestampOrdering { parent: DateTime<Utc>, child: DateTime<Utc> },
    InvalidSnapshotTimestamp { object_store_time: DateTime<Utc>, snapshot_time: DateTime<Utc> },
    OtherFlushError, ConcurrencyError(JoinError),
    Conflict { expected_parent: Option<SnapshotId>, actual_parent: Option<SnapshotId> },
    RebaseFailed { snapshot: SnapshotId, conflicts: Vec<Conflict> },
    SerializationError(..), DeserializationError(..), ConflictingPathNotFound(NodeId),
    InvalidIndex { coords: ChunkIndices, path: Path },
    BadSnapshotChainForDiff,
}

impl Session {
    pub fn from_bytes(bytes: Vec<u8>) -> SessionResult<Self>;
    pub fn as_bytes(&self) -> SessionResult<Vec<u8>>;
    pub fn branch(&self) -> Option<&str>;          // None for a read-only session
    pub fn read_only(&self) -> bool;
    pub fn snapshot_id(&self) -> &SnapshotId;
    pub fn has_uncommitted_changes(&self) -> bool;
    pub fn changes(&self) -> &ChangeSet;
    pub fn config(&self) -> &RepositoryConfig;
    pub fn matching_container(&self, chunk_location: &VirtualChunkLocation)
        -> Option<&VirtualChunkContainer>;
    pub async fn status(&self) -> SessionResult<Diff>;

    pub async fn add_group(&mut self, path: Path, definition: Bytes) -> SessionResult<()>;
    pub async fn update_group(&mut self, path: &Path, definition: Bytes) -> SessionResult<()>;
    pub async fn delete_group(&mut self, path: Path) -> SessionResult<()>;
    pub async fn add_array(&mut self, path: Path, shape: ArrayShape,
                           dimension_names: Option<Vec<DimensionName>>,
                           user_data: Bytes) -> SessionResult<()>;
    pub async fn update_array(&mut self, path: &Path, shape: ArrayShape,
                              dimension_names: Option<Vec<DimensionName>>,
                              user_data: Bytes) -> SessionResult<()>;
    pub async fn delete_array(&mut self, path: Path) -> SessionResult<()>;
    pub async fn delete_node(&mut self, node: NodeSnapshot) -> SessionResult<()>;

    pub async fn get_node(&self, path: &Path) -> SessionResult<NodeSnapshot>;
    pub async fn get_array(&self, path: &Path) -> SessionResult<NodeSnapshot>;
    pub async fn get_group(&self, path: &Path) -> SessionResult<NodeSnapshot>;
    pub async fn get_closest_ancestor_node(&self, path: &Path) -> SessionResult<NodeSnapshot>;
    pub async fn list_nodes(&self)
        -> SessionResult<impl Iterator<Item = SessionResult<NodeSnapshot>> + '_>;

    pub async fn set_chunk_ref(&mut self, path: Path, coord: ChunkIndices,
                               data: Option<ChunkPayload>) -> SessionResult<()>;
    pub async fn get_chunk_ref(&self, path: &Path, coords: &ChunkIndices)
        -> SessionResult<Option<ChunkPayload>>;
    pub async fn get_chunk_reader(&self, path: &Path, coords: &ChunkIndices,
                                  byte_range: &ByteRange)
        -> SessionResult<Option<Pin<Box<dyn Future<Output = SessionResult<Bytes>> + Send>>>>;
    pub fn get_chunk_writer(&self)
        -> impl FnOnce(Bytes) -> Pin<Box<dyn Future<Output = SessionResult<ChunkPayload>> + Send>>;
    pub async fn delete_chunks(&mut self, ..) -> SessionResult<()>;
    pub async fn all_chunks(&self)
        -> SessionResult<impl Stream<Item = SessionResult<(Path, ChunkInfo)>> + '_>;
    pub async fn chunk_coordinates<'a, 'b: 'a>(&'a self, array_path: &'b Path)
        -> SessionResult<impl Stream<Item = SessionResult<ChunkIndices>> + 'a>;
    pub async fn array_chunk_iterator<'a>(&'a self, path: &Path)
        -> impl Stream<Item = SessionResult<ChunkInfo>> + 'a;
    pub async fn all_virtual_chunk_locations(&self)
        -> SessionResult<impl Stream<Item = SessionResult<String>> + '_>;

    pub fn discard_changes(&mut self) -> ChangeSet;
    pub async fn merge(&mut self, changes: ChangeSet) -> SessionResult<()>;
    pub async fn clear(&mut self) -> SessionResult<()>;
    pub async fn commit(&mut self, message: &str, properties: Option<SnapshotProperties>)
        -> SessionResult<SnapshotId>;
    pub async fn rebase(&mut self, solver: &dyn ConflictSolver) -> SessionResult<()>;
}
```

Required behaviour the drivers observe:

- A read-only session has `read_only() == true`, `branch() == None`, and the
  three operations that would publish a session's work — `commit`, `rebase`
  and `merge` — fail with `SessionErrorKind::ReadOnlySession`. The node and
  chunk mutators are **not** separately guarded: they stage into the session's
  own change set exactly as they do on a writable session, and the refusal
  arrives when that change set is published.
- `commit` on a session whose parent is no longer the branch tip fails with
  `SessionErrorKind::Conflict { .. }`.
- `commit` enforces a **strictly increasing** `flushed_at` along a chain: a
  child snapshot's timestamp is always greater than its parent's, or the commit
  fails with `InvalidSnapshotTimestampOrdering`.
- `get_chunk_writer()` inlines a payload of at most
  `RepositoryConfig::inline_chunk_threshold_bytes()` bytes as
  `ChunkPayload::Inline` and writes a larger one as its own chunk object,
  returning `ChunkPayload::Ref`.
- `status()` is the `Diff` of the session's uncommitted changes and is empty for
  a fresh session.
- `commit` on a session that has no changes fails with
  `SessionErrorKind::NoChangesToCommit`; an empty commit is never written.
- The node accessors are typed: `get_array` on a path holding a group fails with
  `SessionErrorKind::NotAnArray`, `get_group` on a path holding an array fails
  with `SessionErrorKind::NotAGroup`, and `add_group` or `add_array` at a path
  that already holds a node fails with `SessionErrorKind::AlreadyExists`. All
  three carry the offending `NodeSnapshot` and a `message` whose text is out of
  scope.
- Node creation does **not** require the ancestor groups to exist:
  `add_group("/g0")` succeeds on a session that has never created the root
  group, and `add_array` behaves the same way. The drivers rely on this — several
  fixtures build `/g0`…`/g4` without a root group.
  `SessionErrorKind::AncestorNodeNotFound { prefix }` belongs to
  `get_closest_ancestor_node`, which reports the deepest existing ancestor of a
  path and fails with that variant when there is none; no requirement group makes it
  a precondition of writing.
- `discard_changes()` takes the change set **out** of the session and returns
  it: afterwards `has_uncommitted_changes()` is `false` and `changes()` is
  empty, while the returned `ChangeSet` still carries the work. It is still
  applicable — `merge(changes)` folds it into another writable session, which
  can then commit it as its own, so the branch gains exactly one snapshot
  covering every merged writer. `merge` on a read-only session fails with
  `SessionErrorKind::ReadOnlySession`. This pair is the distributed-write
  protocol `icechunk-spec/guide/icechunk-python-parallel.md` describes: each
  worker hands its session's changes back, the coordinator merges them, and one
  commit covers them all.
- A session round-trips through `as_bytes()` and `from_bytes()`. The restored
  session carries the same `snapshot_id()` and the same `branch()` as the one it
  was serialised from, and therefore the same `read_only()`: a read-only session
  serialised and restored is still read-only, and a writable one is still
  writable on the same branch.

### 4.12 `icechunk::store`

```rust
// the `store` half of the section 4.2 split, declared in this module
pub type StoreError     = ICError<StoreErrorKind>;
pub type StoreResult<A> = Result<A, StoreError>;

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum SetVirtualRefsResult {
    Done,
    FailedRefs(Vec<ChunkIndices>),
}

#[derive(Debug, Clone, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum ListDirItem { Key(String), Prefix(String) }

impl Store {
    pub async fn from_session(session: Arc<tokio::sync::RwLock<Session>>) -> Self;
    pub fn from_session_and_config(session: Arc<RwLock<Session>>,
                                   get_partial_values_concurrency: u16) -> Self;
    pub fn session(&self) -> Arc<RwLock<Session>>;
    pub async fn read_only(&self) -> bool;
    pub fn from_bytes(bytes: Bytes) -> StoreResult<Self>;
    pub async fn as_bytes(&self) -> StoreResult<Bytes>;

    pub async fn get(&self, key: &str, byte_range: &ByteRange) -> StoreResult<Bytes>;
    pub async fn get_partial_values(
        &self, key_ranges: impl IntoIterator<Item = (String, ByteRange)>
    ) -> StoreResult<Vec<StoreResult<Bytes>>>;
    pub async fn exists(&self, key: &str) -> StoreResult<bool>;
    pub async fn set(&self, key: &str, value: Bytes) -> StoreResult<()>;
    pub async fn set_if_not_exists(&self, key: &str, bytes: Bytes) -> StoreResult<()>;
    pub async fn delete(&self, key: &str) -> StoreResult<()>;
    pub async fn delete_dir(&self, prefix: &str) -> StoreResult<()>;
    pub async fn clear(&self) -> StoreResult<()>;
    pub async fn is_empty(&self, prefix: &str) -> StoreResult<bool>;
    pub async fn list(&self)
        -> StoreResult<impl Stream<Item = StoreResult<String>> + Send>;
    pub async fn list_prefix(&self, prefix: &str)
        -> StoreResult<impl Stream<Item = StoreResult<String>> + Send>;
    pub async fn list_dir(&self, prefix: &str)
        -> StoreResult<impl Stream<Item = StoreResult<String>> + Send>;
    pub async fn list_dir_items(&self, prefix: &str)
        -> StoreResult<impl Stream<Item = StoreResult<ListDirItem>> + Send>;
    pub async fn getsize(&self, key: &str) -> StoreResult<u64>;
    pub async fn getsize_prefix(&self, prefix: &str) -> StoreResult<u64>;   // NEW

    pub async fn set_virtual_ref(&self, key: &str, reference: VirtualChunkRef,
                                 validate_container: bool) -> StoreResult<()>;

    // NEW at this milestone - on `Store`, not on `Session`
    pub async fn set_virtual_refs<I>(&self, array_path: &Path, validate_container: bool,
                                     references: I) -> StoreResult<SetVirtualRefsResult>
    where I: IntoIterator<Item = (ChunkIndices, VirtualChunkRef)> + std::fmt::Debug;
}
```

`set_virtual_refs` semantics, all of it observable and all of it in scope:

1. It takes the session write lock **once** for the whole batch.
2. It addresses the array by `Path` and each chunk by `ChunkIndices`; it never
   parses a Zarr key, so it never reports `StoreErrorKind::InvalidKey`.
3. For each `(index, reference)`: if `validate_container` is `true` and no
   virtual chunk container matches `reference.location`, the **index is
   collected and the reference is skipped** — this is not an error. Otherwise
   the reference is stored as `ChunkPayload::Virtual`.
4. It returns `SetVirtualRefsResult::Done` when nothing was skipped, and
   `SetVirtualRefsResult::FailedRefs(indices)` otherwise, carrying the skipped
   indices in input order. The references that did validate are still applied.
5. On a read-only store it fails early with `StoreErrorKind::ReadOnly`, before
   any reference is considered.
6. A reference written in bulk is indistinguishable from the same reference
   written with `set_virtual_ref`.

`set_virtual_ref`, the single-reference setter, is where point 3's contrast
comes from, and both halves are observable:

- With `validate_container: true` and **no** container matching the reference's
  location it **fails outright** with
  `StoreErrorKind::InvalidVirtualChunkContainer { chunk_location }`, whose field
  carries the location string, and stores nothing — the key is still absent
  afterwards. It does not collect the index the way the bulk setter does.
- A reference stored with `validate_container: false` is accepted unchecked, and
  is refused only when something **reads through it**. That read fails with the
  three-level chain section 7.2's wrapper-variants-carry-the-kind rule produces:
  `StoreErrorKind::SessionError(SessionErrorKind::VirtualReferenceError(`
  `VirtualReferenceErrorKind::NoContainerForUrl(_)))` — not
  `InvalidVirtualChunkContainer`, which belongs to the write path only.

Zarr key grammar the drivers use: `zarr.json` for the root group's metadata,
`<path>/zarr.json` for a node's metadata, and `<array>/c/<i>[/<j>…]` for a chunk.

**A key that was never written is a named absence.** `get` on a metadata key
whose node does not exist, or on a chunk key **of an array that does exist**
that carries no reference, fails with `StoreErrorKind::NotFound(_)` — never a
zero-length success, never an empty `Bytes`, and never a panic. This is the one
negative case in the key-space family, and it holds for a key that was never
written and for one that has been deleted alike. (`getsize` is **not** required
to mirror it: what it reports for a chunk key that carries no reference is not
fixed here, and no group asks. Nor is a chunk key under an array that does
**not** exist this case — there the missing *node* is what is reported, not the
missing chunk; which error that is is not fixed here either, and no group asks.)

The rest of that family carries the meaning its names suggest, and the drivers
hold it to exactly that:

- `exists(key)` **answers** rather than failing: `false` for a key that was
  never written or has since been deleted, `true` otherwise — including a chunk
  key whose virtual reference cannot be fetched; `exists` never reads through a
  reference. `delete(key)` removes the node or the chunk reference at `key`,
  after which `exists` is `false`.
- `set_if_not_exists(key, bytes)` writes only when the key is absent. On a key
  that already holds a value it changes nothing and reports no error, so a
  following `get` returns the **original** bytes, not `bytes`.
- `delete_dir(prefix)` removes everything under `prefix` — the node at that
  path together with every node beneath it, or, when the prefix addresses an
  array's chunk space, the chunks it covers. A prefix that matches nothing is a
  no-op rather than an error.
- `is_empty(prefix)` is `true` exactly when the listing under `prefix` is
  empty, so it is `true` after `delete_dir(prefix)` and `false` while any key
  under `prefix` survives.
- `clear()` empties the whole store, after which `is_empty("")` is `true`.

`getsize` and `getsize_prefix` report **payload** bytes only, never the store's
own overhead:

- `getsize(key)` is the byte length of what `get(key, &ByteRange::ALL)` would
  return — for a metadata key the stored Zarr document exactly as the caller
  supplied it, for a chunk key the chunk's data (the reference's declared
  `length` when it is virtual or native).
- `getsize_prefix(prefix)` is the sum of `getsize` over **every** metadata and
  chunk key under `prefix`. The prefix matches a whole path component at a time,
  so `array` covers `array/zarr.json` and `array/c/0/0/0` but not
  `array-2/zarr.json`, and the empty prefix covers the whole store.

The **listing family** answers the same question by key rather than by size. The
four listings return **different shapes**, and confusing them is the usual way to
fail this section:

- `list()` yields every metadata key and every written chunk key in the store,
  as whole keys.
- `list_prefix(prefix)` yields **whole keys** under `prefix`, matched the same
  whole-component way `getsize_prefix` matches: over a store holding the six
  keys `zarr.json`, `array/zarr.json`, `array/c/0/0/0`, `array/c/1/1/1`,
  `array-2/zarr.json` and `array-2/c/0/0/0`, `list_prefix("array/")` yields the
  three whole `array/…` keys and nothing under `array-2`, while `list()`
  yields all six. **Which** keys come back is fixed; the order they arrive in is
  not (section 10).
- `list_dir(prefix)` yields the distinct **path components** immediately under
  `prefix` — single segments, not whole keys, and with no trailing separator.
  Over the same store, `list_dir("")` yields exactly the three components
  `array`, `array-2` and `zarr.json`, and not the six keys `list()` returns.
  Again the set is fixed and the order is not.
- `list_dir_items(prefix)` is that same listing with each component tagged: a
  component that is itself a stored key is a `ListDirItem::Key`, and one that
  stands for a subtree is a `ListDirItem::Prefix`. `list_dir` is this stream
  with both kinds flattened to the component string. Under a **group** the
  components are that group's own `zarr.json` as a `Key` and each immediate
  child node — a child array included — as a `Prefix`. Under an **array**'s own
  path the answer is fixed at exactly two items, `Key("zarr.json")` and
  `Prefix("c")`, and the `c` prefix is reported **whether or not that array has
  any chunk written**, so it is not evidence that chunks exist.
- `get_partial_values(key_ranges)` answers **one `Result` per requested key, in
  request order**: the outer `StoreResult` reports a global failure of the
  batch, while a key that cannot be read is an `Err` in its own slot and does
  not fail the keys beside it.

`StoreErrorKind` — `#[non_exhaustive]`, wrapper variants carry kinds, and the
absence category it carries is itself an enum:

```rust
#[derive(Debug, Clone, PartialEq, Eq)] #[non_exhaustive]
pub enum KeyNotFoundError {
    ChunkNotFound { key: String, path: Path, coords: ChunkIndices },
    NodeNotFound { path: Path },
    ZarrV2KeyNotFound { key: String },
}

pub enum StoreErrorKind {
    SessionError(SessionErrorKind), RepositoryError(RepositoryErrorKind),
    RefError(RefErrorKind),
    InvalidKey { key: String }, NotAllowed(String), NotFound(KeyNotFoundError),
    MergeError(String), NoSnapshot, PathError(PathError), NotOnBranch,
    BadMetadata(serde_json::Error), DeserializationError(..), SerializationError(..),
    Unimplemented(&'static str), BadKeyPrefix(String), PartialValuesPanic,
    ReadOnly, UncommittedChanges,
    InvalidVirtualChunkContainer { chunk_location: String },
    Other(String), Unknown(Box<dyn std::error::Error + Send + Sync>),
}
```

**Zarr metadata is stored verbatim.** `Store::set` on a metadata key parses only
the fields it needs — for an array, `node_type`, `shape`, `chunk_grid` and
`dimension_names` — and stores the **original bytes** as the node's
`user_data`. Every other key, known to Zarr or not (`data_type`, `fill_value`,
`codecs`, `storage_transformers`, `chunk_key_encoding`, `attributes`, and any
extension key), must survive a `set` → `get` round trip byte for byte, before
and after a commit, and must appear unchanged as `NodeSnapshot::user_data` in
the snapshot.

### 4.13 `icechunk::storage`

```rust
pub async fn new_in_memory_storage() -> StorageResult<Arc<dyn Storage>>;
pub async fn new_local_filesystem_storage(path: &std::path::Path)
    -> StorageResult<Arc<dyn Storage>>;
pub fn new_s3_storage(..) -> StorageResult<Arc<dyn Storage>>;           // still sync
pub async fn new_s3_object_store_storage(..) -> StorageResult<Arc<dyn Storage>>;
pub async fn new_gcs_storage(..)  -> StorageResult<Arc<dyn Storage>>;
pub async fn new_azure_blob_storage(..) -> StorageResult<Arc<dyn Storage>>;

// both structs below derive
// #[derive(Debug, PartialEq, Eq, Serialize, Deserialize, Clone, Default)]
pub struct ConcurrencySettings {
    pub max_concurrent_requests_for_object: Option<NonZeroU16>,
    pub ideal_concurrent_request_size: Option<NonZeroU64>,
}
impl ConcurrencySettings {
    pub fn max_concurrent_requests_for_object(&self) -> NonZeroU16;   // 18
    pub fn ideal_concurrent_request_size(&self) -> NonZeroU64;        // 12 * 1024 * 1024
    pub fn merge(&self, other: Self) -> Self;
}
pub struct Settings { pub concurrency: Option<ConcurrencySettings> }
impl Settings { pub fn concurrency(&self) -> &ConcurrencySettings; pub fn merge(&self, other: Self) -> Self; }

#[derive(Debug, Clone, PartialEq, Eq)] pub struct ETag(pub String);   // was `type ETag = String`
#[derive(Debug, Clone, PartialEq, Eq)] pub struct Generation(pub String);
pub enum VersionInfo { /* … */ }      // storage::VersionInfo, conditional-write plumbing
impl VersionInfo { pub fn for_creation() -> Self; }
// every one of the four result enums below carries the same derive set; of the
// four, `WriteRefResult` is the one whose values the drivers compare with
// `assert_eq!`:
#[derive(Debug, Clone, PartialEq, Eq)] pub enum FetchConfigResult { .. }
#[derive(Debug, Clone, PartialEq, Eq)] pub enum UpdateConfigResult { .. }
#[derive(Debug, Clone, PartialEq, Eq)] pub enum GetRefResult { .. }
#[derive(Debug, Clone, PartialEq, Eq)] pub enum WriteRefResult { Written, WontOverwrite }
pub enum Reader { .. }
pub struct ListInfo<Id> { pub id: Id, pub created_at: DateTime<Utc> }

pub trait Storage: Debug + Sync + Send /* sealed */ {
    fn default_settings(&self) -> Settings;
    async fn fetch_snapshot(&self, settings: &Settings, id: &SnapshotId)
        -> StorageResult<Box<dyn tokio::io::AsyncRead + Unpin + Send>>;
    async fn fetch_manifest_unknown_size(&self, settings: &Settings, id: &ManifestId)
        -> StorageResult<Box<dyn tokio::io::AsyncRead + Unpin + Send>>;
    async fn fetch_manifest_known_size(&self, settings: &Settings, id: &ManifestId, size: u64)
        -> StorageResult<Reader>;
    async fn fetch_transaction_log(&self, settings: &Settings, id: &SnapshotId)
        -> StorageResult<Box<dyn tokio::io::AsyncRead + Unpin + Send>>;
    async fn fetch_chunk(&self, settings: &Settings, id: &ChunkId,
                         range: &Range<ChunkOffset>) -> StorageResult<Bytes>;
    async fn write_snapshot(&self, settings: &Settings, id: SnapshotId,
                            metadata: Vec<(String, String)>, bytes: Bytes) -> StorageResult<()>;
    async fn write_manifest(&self, settings: &Settings, id: ManifestId,
                            metadata: Vec<(String, String)>, bytes: Bytes) -> StorageResult<()>;
    async fn write_transaction_log(&self, settings: &Settings, id: SnapshotId,
                                   metadata: Vec<(String, String)>, bytes: Bytes)
        -> StorageResult<()>;
    async fn write_chunk(&self, settings: &Settings, id: ChunkId, bytes: Bytes)
        -> StorageResult<()>;
    async fn get_ref(&self, settings: &Settings, ref_key: &str) -> StorageResult<GetRefResult>;
    async fn ref_names(&self, settings: &Settings) -> StorageResult<Vec<String>>;
    async fn write_ref(&self, settings: &Settings, ref_key: &str, bytes: Bytes,
                       previous_version: &VersionInfo) -> StorageResult<WriteRefResult>;
    // note: `root_is_clean` is the one method here that takes NO `settings`
    async fn root_is_clean(&self) -> StorageResult<bool>;
    async fn list_snapshots(..) / list_manifests(..) / list_chunks(..) /
             list_transaction_logs(..) / delete_* (..) / ..;
}
```

The `// 18` and `// 12 * 1024 * 1024` above are the **struct** fall-backs —
what `ConcurrencySettings`' getters answer when the fields are `None`. They are
not a promise about any particular backend: `default_settings()` is implemented
per backend and the in-memory and local-filesystem stores both report
concurrency of their own. What a repository owes is only that
`storage_settings()` agrees with its backend's `default_settings()` when the
configuration sets none.

`Storage` must be **object-safe**: every constructor above returns
`Arc<dyn Storage>`, and the drivers hold `Arc<dyn Storage + Send + Sync>` and
call the `async fn`s above through it. The bare `async fn` spelling in the block
above is shorthand for whatever makes that work on the pinned toolchain — this
release uses `#[async_trait]`.

The `Storage` trait is **sealed**: an external crate cannot implement it, so the
drivers never supply their own backend. They use the two offline constructors
above, and they read and write raw objects through the trait's own
`fetch_*`/`write_*` methods — which is how section 5's byte layout is observed.

`Storage::write_snapshot` and friends **overwrite**: writing an object under an
id that already exists replaces it. The drivers rely on that to stage a
corrupted object.

`Storage::write_ref` does **not** overwrite. Its `ref_key` is relative to the
`refs/` prefix (`"branch.main/ref.json"`, `"tag.v1/ref.json"`), and
`previous_version` makes the put conditional: with
`storage::VersionInfo::for_creation()` it is create-if-absent, returning
`WriteRefResult::Written` when it wrote and `WriteRefResult::WontOverwrite` when
an object already exists at that key. That conditional put is the mechanism the
branch move in section 5.4 is built on.

The object-store key layout, unchanged since milestone 1: `$ROOT/refs/`,
`$ROOT/snapshots/`, `$ROOT/attributes/`, `$ROOT/manifests/`,
`$ROOT/transactions/`, `$ROOT/chunks/`. On a local-filesystem store these are
real directories under the root path, and the drivers look at them.

### 4.14 `refs`, `ops::gc`, `conflicts`, `virtual_chunks`, `config`

Inherited surface the drivers name directly, in this release's spelling:

```rust
// icechunk::refs
#[derive(Debug, Clone, Eq, PartialEq, Hash, PartialOrd, Ord)]
pub enum Ref { Tag(String), Branch(String) }
impl Ref {
    pub const DEFAULT_BRANCH: &'static str = "main";
    pub fn is_tag(&self) -> bool;   pub fn is_branch(&self) -> bool;
    pub async fn fetch(&self, storage: &(dyn Storage + Send + Sync),
                       settings: &storage::Settings) -> RefResult<RefData>;
}
#[derive(Debug, Clone, Eq, PartialEq, Serialize, Deserialize)]
pub struct RefData { pub snapshot: SnapshotId }
pub async fn create_tag(storage, settings, name: &str, snapshot: SnapshotId) -> RefResult<()>;
pub async fn update_branch(storage, settings, name: &str, new_snapshot: SnapshotId,
                           current_snapshot: Option<&SnapshotId>) -> RefResult<()>;
pub async fn fetch_tag(storage, settings, name: &str) -> RefResult<RefData>;
pub async fn fetch_branch_tip(storage, settings, name: &str) -> RefResult<RefData>;
pub async fn list_refs(storage, settings) -> RefResult<BTreeSet<Ref>>;
pub async fn list_tags(storage, settings) -> RefResult<BTreeSet<String>>;
pub async fn list_branches(storage, settings) -> RefResult<BTreeSet<String>>;
pub async fn delete_branch(..) / delete_tag(..);
#[derive(Debug, thiserror::Error)]
pub enum RefErrorKind { Storage(StorageErrorKind), RefNotFound(String),
    InvalidRefType(String), InvalidRefName(String), TagAlreadyExists(String),
    Serialization(serde_json::Error),
    Conflict { expected_parent: Option<SnapshotId>, actual_parent: Option<SnapshotId> } }
pub type RefError = ICError<RefErrorKind>;
pub type RefResult<A> = Result<A, RefError>;

// icechunk::ops::gc
pub enum Action { Keep, DeleteIfCreatedBefore(DateTime<Utc>) }
pub struct GCConfig { /* private */ }
impl GCConfig {
    pub fn new(extra_roots: HashSet<SnapshotId>, dangling_chunks: Action,
               dangling_manifests: Action, dangling_attributes: Action,
               dangling_transaction_logs: Action, dangling_snapshots: Action) -> Self;
    pub fn clean_all(chunks_age: DateTime<Utc>, metadata_age: DateTime<Utc>,
                     extra_roots: Option<HashSet<SnapshotId>>) -> Self;
    pub fn deletes_chunks(&self) -> bool;      pub fn deletes_manifests(&self) -> bool;
    pub fn deletes_attributes(&self) -> bool;  pub fn deletes_transaction_logs(&self) -> bool;
    pub fn deletes_snapshots(&self) -> bool;
}
#[derive(Debug, PartialEq, Eq, Default)]
pub struct GCSummary { pub chunks_deleted: usize, pub manifests_deleted: usize,
                       pub snapshots_deleted: usize, pub attributes_deleted: usize,
                       pub transaction_logs_deleted: usize }
pub enum GCError { .. }                       // its variants are not asserted
pub type GCResult<A> = Result<A, GCError>;    // `GCError: std::error::Error + 'static`
pub async fn garbage_collect(storage: &(dyn Storage + Send + Sync),
                             storage_settings: &storage::Settings,
                             asset_manager: Arc<AssetManager>, config: &GCConfig)
    -> GCResult<GCSummary>;
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum ExpiredRefAction { Delete, Ignore }
#[derive(Debug, PartialEq, Eq, Clone)]
pub enum ExpireRefResult { RefIsExpired, NothingToDo,
                           Done { released_snapshots: HashSet<SnapshotId>,
                                  edited_snapshot: SnapshotId } }
#[derive(Debug, PartialEq, Eq, Clone, Default)]
pub struct ExpireResult { pub released_snapshots: HashSet<SnapshotId>,
                          pub edited_snapshots: HashSet<SnapshotId>,
                          pub deleted_refs: HashSet<Ref> }
pub async fn expire(storage, storage_settings, asset_manager: Arc<AssetManager>,
                    older_than: DateTime<Utc>, expired_branches: ExpiredRefAction,
                    expired_tags: ExpiredRefAction) -> GCResult<ExpireResult>;
pub async fn expire_ref(storage, storage_settings, asset_manager, reference: &Ref,
                        older_than: DateTime<Utc>) -> GCResult<ExpireRefResult>;

// icechunk::conflicts
pub enum Conflict { NewNodeConflictsWithExistingNode(Path), NewNodeInInvalidGroup(Path),
    ZarrMetadataDoubleUpdate(Path), ZarrMetadataUpdateOfDeletedArray(Path),
    ZarrMetadataUpdateOfDeletedGroup(Path),
    ChunkDoubleUpdate { path: Path, node_id: NodeId, chunk_coordinates: HashSet<ChunkIndices> },
    ChunksUpdatedInDeletedArray { path: Path, node_id: NodeId },
    ChunksUpdatedInUpdatedArray { path: Path, node_id: NodeId },
    DeleteOfUpdatedArray { path: Path, node_id: NodeId },
    DeleteOfUpdatedGroup { path: Path, node_id: NodeId } }
pub enum ConflictResolution { Patched(ChangeSet), Unsolvable { reason: Vec<Conflict>,
                                                               unmodified: ChangeSet } }
pub trait ConflictSolver { .. }
pub mod detector { pub struct ConflictDetector; }            // never patches; reports
pub mod basic_solver {
    pub enum VersionSelection { Fail, UseOurs, UseTheirs }
    pub struct BasicConflictSolver {
        pub on_chunk_conflict: VersionSelection,
        pub fail_on_delete_of_updated_array: bool,
        pub fail_on_delete_of_updated_group: bool,
    }
    impl Default for BasicConflictSolver;   // UseOurs, false, false
    // `on_user_attributes_conflict` is GONE at this milestone: user attributes
    // are part of `user_data` and no longer conflict on their own.
}

// icechunk::virtual_chunks
pub type ContainerName = String;
pub struct VirtualChunkContainer { pub name: ContainerName, pub url_prefix: String,
                                   pub store: ObjectStoreConfig }
pub fn mk_default_containers() -> HashMap<ContainerName, VirtualChunkContainer>;

// icechunk::config
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq, Default)]
pub struct RepositoryConfig {          // every field optional
    pub inline_chunk_threshold_bytes: Option<u16>,
    pub get_partial_values_concurrency: Option<u16>,
    pub compression: Option<CompressionConfig>,
    pub caching: Option<CachingConfig>,
    pub storage: Option<storage::Settings>,
    pub virtual_chunk_containers: Option<HashMap<ContainerName, VirtualChunkContainer>>,
    pub manifest: Option<ManifestConfig>,
}
impl RepositoryConfig {
    pub fn inline_chunk_threshold_bytes(&self) -> u16;       // 512
    pub fn get_partial_values_concurrency(&self) -> u16;     // 10
    pub fn virtual_chunk_containers(&self) -> impl Iterator<Item = &VirtualChunkContainer>;
    pub fn compression(&self) -> &CompressionConfig;
    pub fn caching(&self) -> &CachingConfig;
    pub fn storage(&self) -> Option<&storage::Settings>;
    pub fn manifest(&self) -> &ManifestConfig;
    pub fn merge(&self, other: RepositoryConfig) -> RepositoryConfig;
    pub fn set_virtual_chunk_container(&mut self, cont: VirtualChunkContainer);
    pub fn get_virtual_chunk_container(&self, name: &str) -> Option<&VirtualChunkContainer>;
    pub fn clear_virtual_chunk_containers(&mut self);
}

// The three configuration structs below derive exactly this set, `Default`
// included, so a caller may write
// `CachingConfig { num_chunk_refs: Some(100), ..Default::default() }`:
#[derive(Debug, PartialEq, Eq, Serialize, Deserialize, Clone, Default)]

pub struct CachingConfig {
    pub num_snapshot_nodes: Option<u64>,       // 10_000
    pub num_chunk_refs: Option<u64>,           // 5_000_000
    pub num_transaction_changes: Option<u64>,  // 0
    pub num_bytes_attributes: Option<u64>,     // 0
    pub num_bytes_chunks: Option<u64>,         // 0
}
impl CachingConfig { /* one getter per field, answering the value above when the
                        field is `None`, plus `merge(&self, other: Self) -> Self` */ }

pub struct CompressionConfig { pub algorithm: Option<CompressionAlgorithm>, pub level: Option<u8> }
impl CompressionConfig { pub fn algorithm(&self) -> CompressionAlgorithm;   // Zstd
                         pub fn level(&self) -> u8;   // a fixed built-in default; its
                                                      // value is out of scope, section 10
                         pub fn merge(&self, other: Self) -> Self; }

pub struct ManifestConfig { /* declared; nothing here fixes its shape */ }

#[derive(Debug, PartialEq, Eq, Default, Serialize, Deserialize, Clone, Copy)]
#[serde(rename_all = "snake_case")]
pub enum CompressionAlgorithm { #[default] Zstd }

pub enum Credentials { S3(..), Gcs(..), Azure(..) }
pub enum ObjectStoreConfig { InMemory, LocalFileSystem(PathBuf), S3(..), S3Compatible(..),
                             Gcs(..), Azure(..), Tigris(..) }
```

A branch or tag name containing `/` is **refused wherever a reference key is
derived from that name** — `create_branch`, `create_tag`, `update_branch`,
`fetch_tag`, `fetch_branch_tip`, `delete_branch` and `delete_tag` alike — with
`RefErrorKind::InvalidRefName(_)`, which surfaces as
`RepositoryErrorKind::Ref(RefErrorKind::InvalidRefName(_))` where the call goes
through `Repository`. This is a **name** rejection, not an absence: the answer
is `InvalidRefName` on the **read** path exactly as on the write path, and never
the `RefNotFound` that section 4.10 gives for a well-formed name that does not
resolve. No reference file is written, read or left behind for such a name. `PROFILE.md` P3 states the
underlying rule; this paragraph fixes the error it is reported with.

`BasicConflictSolver::on_chunk_conflict` decides a `ChunkDoubleUpdate` and
nothing else, and its three values name **whose** value survives at the
conflicting coordinates: `UseOurs` keeps the **rebasing** session's value, the
one not yet committed, by leaving its change in place; `UseTheirs` keeps the
value **already committed** on the branch, by dropping the rebasing session's
change at exactly those coordinates; `Fail` refuses to patch at all — `rebase`
restores the session's change set unmodified and fails with
`SessionErrorKind::RebaseFailed { snapshot, conflicts }`, carrying the
`Conflict` values that were found. `fail_on_delete_of_updated_array` and
`fail_on_delete_of_updated_group` are the same switch for the two delete
conflicts, and every other `Conflict` variant is unsolvable for this solver
whatever the three fields say. `ConflictDetector` never patches anything: it
reports, so `rebase` with it always fails when there is a conflict at all.

`RepositoryConfig` and `CachingConfig` are both **`Default`**, and both defaults
are used directly: the drivers build a configuration with
`icechunk::RepositoryConfig::default()` and hand
`icechunk::config::CachingConfig::default()` to
`AssetManager::new_with_config` (section 4.9). `CachingConfig` must therefore be
public at `icechunk::config` under that name. Its five fields above are the
cache budgets, and each getter answers the commented value when the field is
`None`, so an all-default `CachingConfig` is a usable configuration rather than
an empty one.

`ManifestConfig` and the preload configuration it carries are part of the
declared surface — `RepositoryConfig` owns a `manifest` field and must
round-trip through serde with it — but nothing at this milestone asserts their
shape or their behaviour.

`RepositoryConfig::default()` ships virtual chunk containers for the prefixes
`s3`, `gcs`, `az`, `tigris` and `file`; matching is longest-prefix-first over
`str::starts_with`, so `file:///tmp/x.nc` matches the `file` container out of
the box and `weird://…` matches nothing.
`virtual_chunks::mk_default_containers()` is where that set comes from: it
returns a map of exactly those five containers, each entry **keyed by its own
`name`**, and each entry's `url_prefix` is that same string. So
`mk_default_containers()` has an entry under the key `"file"`, and that
container's `name` is `"file"` and its `url_prefix` is `"file"`.

Expiration and garbage collection behave as
`icechunk-spec/design/007-basic-expiration.md` describes, and the drivers
observe exactly that:

- `expire` walks each reference's ancestry. Snapshots strictly older than
  `older_than` are **released**, except the chain root, which is never released.
  The oldest *kept* snapshot is rewritten **in place, keeping its id**, with the
  root as its new parent ("update A in place"), and it is reported in
  `edited_snapshots`. Nothing is deleted at this stage.
- If a reference itself points at an already-expired snapshot, `expire` applies
  the `ExpiredRefAction` for that reference kind: `Ignore` leaves the reference
  and its history untouched and contributes nothing, `Delete` removes the
  reference and records it in `deleted_refs`.
- `garbage_collect` deletes exactly the objects **no branch and no tag can
  reach**, and reports the counts per object kind in `GCSummary`. A `GCConfig`
  whose actions are all `Action::Keep` deletes nothing and reports all zeros.
- `GCConfig::clean_all(chunks_age, metadata_age, extra_roots)` is the
  complement of that: it fills **all five** actions with
  `Action::DeleteIfCreatedBefore` — chunks at `chunks_age`, and manifests,
  attributes, transaction logs and snapshots at `metadata_age` — so all five
  `deletes_*` predicates answer `true` on the configuration it returns.
  `extra_roots` of `None` means "no extra roots".
- A transaction log is addressed by the id of the snapshot it belongs to.
  **When the same `GCConfig` is also deleting snapshots**, a transaction log
  shares that snapshot's reachability exactly: it is kept while its snapshot is
  kept and becomes collectable with it, so collecting *n* unreachable snapshots
  collects their *n* transaction logs in the same pass and
  `GCSummary::transaction_logs_deleted` counts them. A configuration that
  collects transaction logs **without** also collecting snapshots keeps no
  snapshot alive for this purpose, and so collects every transaction log past
  its age cut-off whether or not its snapshot is reachable. `clean_all` sets
  both, which is the configuration the groups here use.
- `expire_ref` is `expire` narrowed to a single reference, and it answers with
  `ExpireRefResult`. When there is history to squash the answer is
  `Done { released_snapshots, edited_snapshot }`: `edited_snapshot` is the id of
  the **oldest kept** snapshot — the one rewritten in place onto the root, and
  note the field is singular, where `expire`'s aggregate `ExpireResult` reports a
  set as `edited_snapshots` — and `released_snapshots` holds the expired
  ancestors with the chain root excluded. When the oldest kept snapshot **is**
  the root, or is already the root's direct child, there is nothing to squash and
  the answer is `NothingToDo`.
- When the reference's **own tip** is already older than `older_than`,
  `expire_ref` rewrites nothing and releases nothing, and answers
  `ExpireRefResult::RefIsExpired`. So a tag created at a snapshot that an earlier
  expiration already released reports `RefIsExpired` rather than expiring
  anything further.
- A tag therefore keeps its snapshots alive: a snapshot that expiration released
  from a branch's history is still collected-around while any tag points into
  it.
- Because a rewritten snapshot keeps its id, an `AssetManager` that performed
  the expiration serves the new version afterwards.

`RepositoryConfig::unsafe_overwrite_refs` is **gone** at this milestone
(section 7.1).

---

## 5. The on-disk byte layout of a metadata object

A snapshot, manifest or transaction-log object in object storage is exactly:

```text
[ 39-byte PLAINTEXT header ][ compressed payload ]
                              └─ decompresses to the finished FlatBuffers root buffer
```

### 5.1 The header

Never compressed. Byte-for-byte:

| byte range | content | value written at this release |
| --- | --- | --- |
| 0–11 | magic | `ICE🧊CHUNK` UTF-8 = `49 43 45 f0 9f a7 8a 43 48 55 4e 4b` (12 bytes) |
| 12–35 | implementation id, UTF-8, right-padded with ASCII spaces to exactly 24 bytes | any non-empty ASCII identification of the writing client |
| 36 | spec version (`SpecVersionBin as u8`) | `0x01` |
| 37 | file type (`FileTypeBin as u8`) | `0x01` snapshot, `0x02` manifest, `0x04` transaction log |
| 38 | compression (`CompressionAlgorithmBin as u8`) | `0x01` (Zstd) for all three metadata types |
| 39.. | the compressed payload | a zstd frame — its first four bytes are the zstd frame magic `28 b5 2f fd` |

The reader consumes exactly those 39 bytes, in that order, and **validates each
field as soon as its own bytes have been read, before reading the next one**. So
a short object fails on the first field it cannot complete *or* that is invalid,
whichever comes first: an object whose first twelve bytes are not the magic is
`InvalidMagicNumbers` however short it is — twenty bytes of arbitrary text is
`InvalidMagicNumbers`, not `IOError` — while an object that carries the magic and
then runs out of bytes is `IOError(_)`. Failures map to:

| condition | error |
| --- | --- |
| bytes 0–11 are not the magic | `RepositoryErrorKind::FormatError(IcechunkFormatErrorKind::InvalidMagicNumbers)` |
| bytes 12–35 | read and **ignored** — a file written by a different implementation still reads |
| byte 36 is not a known `SpecVersionBin` | `FormatError(IcechunkFormatErrorKind::InvalidSpecVersion)` |
| byte 37 is not a known `FileTypeBin`, **or** is a known one that differs from the expected type | `FormatError(InvalidFileType { expected, got })` — `expected` is the type being fetched, `got` is the raw byte |
| byte 38 is not a known `CompressionAlgorithmBin` | `FormatError(IcechunkFormatErrorKind::InvalidCompressionAlgorithm)` |
| the object runs out of bytes before a field above is complete — an object shorter than 39 bytes whose header is otherwise valid | `RepositoryErrorKind::IOError(_)` |
| the header is complete but the payload is not a readable frame | `FormatError(IcechunkFormatErrorKind::IO(_))` |

The header is **the only** thing that discriminates file types: there is no
per-type marker inside the payload (section 5.2). A writer stamps **the same**
identification into every object it writes, so the snapshot, manifest and
transaction log a single client produces agree byte for byte over bytes 0–36 and
differ only at byte 37. What that identification *says* is the writer's business
and is out of scope (section 10).

Writers also record the same facts as object-store metadata under the
`format_constants` keys. Icechunk writes them and does not read them; nothing about them is
in scope.

### 5.2 The payload

Everything from byte 39 to the end is a **self-contained compressed frame**, and
its plaintext is the **bare, non-size-prefixed FlatBuffers root buffer** —
byte-identical to what `Snapshot::bytes()` / `Manifest::bytes()` /
`TransactionLog::bytes()` return, with no Icechunk prefix, length prefix or
trailer of any kind inside it. The drivers check this by reconstructing the
39-byte header from the public constants, splicing the object's bytes 39.. behind
it, storing the result and requiring the decoded object's `bytes()` to equal the
original's.

Inside that buffer:

- bytes 0–3: the `uoffset_t` to the root table;
- bytes 4–7: the 4-byte FlatBuffers **file identifier `"Ichk"`** (`49 63 68 6b`),
  identical for all three file types;
- bytes 8..: vtables and tables.

`"Ichk"` is **written but never verified on read**, because no `file_identifier`
is declared in any `.fbs` file (section 6). A buffer whose bytes 4..8 have been
overwritten still verifies and still decodes. Conversely, a *manifest* buffer
handed to `Snapshot::from_buffer` is rejected — structural verification, not the
identifier, is what separates the three types.

A size-prefixed FlatBuffers encoding is **not** accepted: prefixing the buffer
with its length makes `from_buffer` fail.

### 5.3 Verification runs once, before any access

`Snapshot::from_buffer`, `Manifest::from_buffer` and
`TransactionLog::from_buffer` run the FlatBuffers verifier over the whole buffer
**once**, and then keep only the owned `Vec<u8>`. Every later accessor uses the
unchecked fast path, which is sound precisely because the value cannot exist
without having been verified.

Consequences the drivers require:

- A truncated, empty, tiny or byte-corrupted buffer produces
  `IcechunkFormatErrorKind::InvalidFlatBuffer(_)` — never a panic, never an
  out-of-bounds read, never a silently wrong value.
- An object already in storage whose bytes were replaced by garbage produces a
  named error through `AssetManager::fetch_*`, not a panic.
- Because `deserialize_X` calls `X::from_buffer`, verification also happens
  before `deserialize_X` returns.
- After a successful `from_buffer`, **every** accessor on the value is safe to
  call.

The verifier limits this release uses (depth 64; 500 000 tables for snapshots,
50 000 000 for manifests and transaction logs; apparent size `1 << 31`; missing
null terminators ignored) are implementation policy and are out of scope; what is
in scope is that verification happens and that its failure is the named error.

### 5.4 Reading a repository written before this release

The on-disk format changed incompatibly at this release, and there are two
distinct, both-observable diagnostics.

**Repository level.** Branch references moved from sequence-numbered files
(`refs/branch.$NAME/$ENCODED_SEQUENCE.json`) to a single
`refs/branch.$NAME/ref.json` updated with a conditional put. A repository
written by the previous release therefore has no `refs/branch.main/ref.json`, so
`Repository::exists` is `false` and `Repository::open` fails with
`RepositoryErrorKind::RepositoryDoesntExist`.

*Tags did not move*: a tag reference is still `refs/tag.$NAME/ref.json`, exactly
as before.

**File level.** A metadata file written by the previous release still carries a
valid 39-byte header with spec version `1` — the spec version byte did **not**
change — so it clears every header check and is then rejected by the payload
verification with
`IcechunkFormatErrorKind::InvalidFlatBuffer(_)`, because its payload is not a
FlatBuffers root.

There is no migration path in this release: data written by a previous version
must be re-ingested.

---

## 6. The FlatBuffers schemas

The normative IDL ships at `/app/icechunk-spec/flatbuffers/` — five files,
`namespace gen`, generated with

```text
flatc --rust -o <out> --gen-all all.fbs
```

**`flatc` is not installed in this workspace and nothing fetches it for you.**
The command above records how the shipped `.fbs` files map onto the module
section 4.8 requires; it is not an obligation to run that exact tool. What is
in scope is the module — a `format::flatbuffers::gen` exporting the items section
4.8 names, with the values and signatures it gives — and **any** module that
does so satisfies the contract, however it was produced: `flatc` if you install
one, a build script, a vendored copy of the generated file, or hand-written
Rust. No requirement group inspects how the module was generated.

| file | contents |
| --- | --- |
| `all.fbs` | no declarations; `include`s `object_ids.fbs`, `manifest.fbs`, `snapshot.fbs`, `transaction_log.fbs` in that order. It is the single compilation unit, a workaround for `flatc`'s Rust multi-file generation |
| `object_ids.fbs` | `struct ObjectId12 { bytes:[uint8:12] }` (snapshot, manifest, chunk, attributes ids) and `struct ObjectId8 { bytes:[uint8:8] }` (node ids) |
| `snapshot.fbs` | `MetadataItem`, `ManifestFileInfo`, `ChunkIndexRange`, `ManifestRef`, `DimensionShape`, `DimensionName`, `GroupNodeData`, `ArrayNodeData`, `union NodeData`, `NodeSnapshot`, `Snapshot`; `root_type Snapshot` |
| `manifest.fbs` | `ChunkRef`, `ArrayManifest`, `Manifest`; `root_type Manifest` |
| `transaction_log.fbs` | `ChunkIndices`, `ArrayUpdatedChunks`, `TransactionLog`; `root_type TransactionLog` |

**No `.fbs` file declares a `file_identifier`.** The writer still passes `"Ichk"`
to `finish`, so the four bytes are present at offset 4 of every buffer, but no
`root_as_*` helper and no `*_IDENTIFIER` constant is generated, and nothing
checks them on read. See section 5.2.

Ordering invariants the schemas state. The **keyed** lookups are binary searches
over these vectors and depend on the order directly — `Snapshot::get_node` over
`Snapshot.nodes`, and the manifest's array and chunk-reference lookups over
`Manifest.arrays` and `ArrayManifest.refs`. The remaining orders are required by
the schema and are observable in the decoded vectors however an accessor scans
them; `Snapshot::get_manifest_file`, for one, is a plain linear search:

- `Snapshot.metadata` ascending by `MetadataItem.name`;
- `Snapshot.manifest_files` ascending by `ManifestFileInfo.id`;
- `Snapshot.nodes` ascending by `NodeSnapshot.path` (not written in the `.fbs`,
  but `Snapshot::from_iter` requires its input already sorted by path and
  `get_node` looks up by key);
- `ArrayManifest.refs` ascending by `ChunkRef.index`;
- `Manifest.arrays` ascending by `ArrayManifest.node_id`;
- `ArrayUpdatedChunks.chunks` ascending lexicographically;
- `TransactionLog`'s six node-id vectors ascending.

Two schema details worth stating because a reader will trip on them:

- `ChunkRef` deliberately uses **no union**: exactly one of `chunk_id`,
  `location` or `inline` is expected to be present, and at most one of
  `checksum_etag` / `checksum_last_modified`. A `ChunkRef` with none of the
  first three is a decoding error in the `InvalidFlatBuffer` category.
- In `transaction_log.fbs` the comments above `updated_arrays` and
  `updated_groups` are swapped relative to the field names. The **field names**
  are what the writer and reader match on; the comment swap has no runtime
  effect. `PROFILE.md` records this.

---

## 7. Migration from the previous milestone

Everything below changed name, module path, or signature, or was removed,
between the previous checkpoint and this one. The new form is what is required;
the old form is not accepted.

### 7.1 Removed outright

| Previous milestone | This milestone |
| --- | --- |
| `icechunk::metadata` (whole module) | **REMOVED** |
| `metadata::data_type::DataType` | REMOVED — a Zarr dtype is no longer modelled; it lives inside opaque `user_data` bytes |
| `metadata::fill_value::FillValue` (+ `NAN_STR`, `INF_STR`, `NEG_INF_STR`, `from_data_type_and_json`, `get_data_type`) | REMOVED |
| `metadata::{Codec, StorageTransformer, ChunkShape, ChunkKeyEncoding}` | REMOVED |
| `metadata::UserAttributes` (+ `try_new`, `to_bytes`) | REMOVED |
| `metadata::{ArrayShape = Vec<u64>, DimensionName = Option<String>, DimensionNames}` | REMOVED; replaced by the **different, non-alias** types `format::snapshot::{ArrayShape, DimensionName}` |
| `format::snapshot::ZarrArrayMetadata` (8 public fields) | **REMOVED** — replaced by `NodeData::Array { shape, dimension_names, manifests }` plus `NodeSnapshot::user_data: Bytes` |
| `format::snapshot::{UserAttributesRef, UserAttributesSnapshot}` | REMOVED |
| `format::snapshot::AttributeFileInfo`, `Snapshot::attribute_files()` | REMOVED |
| `format::serializers::current` and every `XSerializer` / `XDeserializer` in it | **REMOVED** (the whole submodule) |
| `store::ZarrArrayMetadataSerialzer` | REMOVED |
| `refs::BranchVersion(pub u64)` | **REMOVED** — branch refs are no longer sequence-numbered |
| `refs::fetch_ref` | REMOVED (`fetch_tag` and `fetch_branch_tip` remain) |
| `RefErrorKind::InvalidBranchVersion(String)` | REMOVED |
| `RepositoryConfig::unsafe_overwrite_refs` field and accessor | REMOVED |
| `Session::set_user_attributes` | REMOVED — attributes are part of `user_data` |
| `Manifest::new(BTreeMap<..>)`, `impl Default for Manifest`, `impl PartialEq for Manifest` | REMOVED |
| `Snapshot::nodes()`, `Snapshot::from_fields(..)` | REMOVED — use `iter()` / `get_node()` / `from_iter(..)` |
| `ObjectStorage::from_config(..)`, `ObjectStorageConfig` | REMOVED — use `new_s3_*` / `new_gcs_storage` / `new_azure_blob_storage` |
| `config::CredentialsFetcher` | RENAMED `config::S3CredentialsFetcher` |
| `change_set::{get_user_attributes, has_updated_attributes, undo_user_attributes_update, update_user_attributes, user_attributes_updated_nodes, zarr_updated_arrays}` | REMOVED or renamed — see 7.6 |

### 7.2 Every error enum split into `…ErrorKind` + `ICError`

A milestone-3 test that wrote `Err(SessionError::NodeNotFound { .. })` or
`matches!(e, RepositoryError::FormatError(_))` no longer compiles.

| Previous milestone | This milestone |
| --- | --- |
| `enum format::IcechunkFormatError` | `enum IcechunkFormatErrorKind` + `type IcechunkFormatError = ICError<IcechunkFormatErrorKind>` |
| `enum repository::RepositoryError` | `enum RepositoryErrorKind` + `type RepositoryError = ICError<RepositoryErrorKind>` |
| `enum session::SessionError` | `enum SessionErrorKind` + `type SessionError = ICError<SessionErrorKind>` |
| `enum store::StoreError` | `enum StoreErrorKind` + `type StoreError = ICError<StoreErrorKind>` |
| `enum storage::StorageError` | `enum StorageErrorKind` + `type StorageError = ICError<StorageErrorKind>` |
| `enum refs::RefError` | `enum RefErrorKind` + `type RefError = ICError<RefErrorKind>` |
| `enum format::manifest::VirtualReferenceError` | `enum VirtualReferenceErrorKind` + `type VirtualReferenceError = ICError<VirtualReferenceErrorKind>` |

The new match shape:

```rust
// previous milestone
assert!(matches!(back, Err(RefError::RefNotFound(r)) if r == "non-existing-tag"));
// this milestone
assert!(matches!(back, Err(RefError { kind: RefErrorKind::RefNotFound(r), .. }) if r == "non-existing-tag"));
```

Variant-level changes:

- `RepositoryErrorKind`'s wrapper variants are `StorageError(StorageErrorKind)`,
  `FormatError(IcechunkFormatErrorKind)` and `Ref(RefErrorKind)` — they carry the
  **kind**, not the outer alias. `ConfigDeserializationError` moved from
  `serde_yml::Error` to `serde_yaml_ng::Error`. `ConfigWasUpdated` is new.
- `SessionErrorKind` gains wrapper variants `RepositoryError`, `StorageError`,
  `FormatError`, `Ref`, `VirtualReferenceError` (all carrying kinds) and the new
  leaves `AncestorNodeNotFound { prefix }`, `InvalidIndex { coords, path }`,
  `BadSnapshotChainForDiff`, `NoChangesToCommit`.
- `StoreErrorKind` gains wrapper variants plus
  `InvalidVirtualChunkContainer { chunk_location }`, `UncommittedChanges`,
  `Other(String)`, `Unknown(Box<dyn Error + Send + Sync>)`.
- `IcechunkFormatErrorKind` gains `InvalidFlatBuffer(..)` and `InvalidTimestamp`.

### 7.3 `format::snapshot::Snapshot` — every accessor changed

| Previous milestone | This milestone |
| --- | --- |
| `fn id(&self) -> &SnapshotId` | `fn id(&self) -> SnapshotId` |
| `fn parent_id(&self) -> &Option<SnapshotId>` | `fn parent_id(&self) -> Option<SnapshotId>` |
| `fn metadata(&self) -> &SnapshotProperties` | `fn metadata(&self) -> IcechunkResult<SnapshotProperties>` |
| `fn flushed_at(&self) -> &DateTime<Utc>` | `fn flushed_at(&self) -> IcechunkResult<DateTime<Utc>>` |
| `fn message(&self) -> &String` | `fn message(&self) -> String` |
| `fn nodes(&self) -> &BTreeMap<Path, NodeSnapshot>` | **REMOVED** — use `iter()` / `get_node()` |
| `fn get_node(&self, path) -> IcechunkResult<&NodeSnapshot>` | `-> IcechunkResult<NodeSnapshot>` |
| `fn iter(&self) -> impl Iterator<Item = &NodeSnapshot>` | `-> impl Iterator<Item = IcechunkResult<NodeSnapshot>>` |
| `fn iter_arc(self: Arc<Self>) -> impl Iterator<Item = NodeSnapshot>` | `-> impl Iterator<Item = IcechunkResult<NodeSnapshot>>` |
| `fn manifest_files(&self) -> &HashMap<ManifestId, ManifestFileInfo>` | `-> impl Iterator<Item = ManifestFileInfo> + '_` |
| `fn get_manifest_file(&self, id) -> Option<&ManifestFileInfo>` | `-> Option<ManifestFileInfo>` |
| `fn manifest_info(&self, id) -> Option<&ManifestFileInfo>` | `-> Option<ManifestFileInfo>` |
| `fn initial() -> Self` | `fn initial() -> IcechunkResult<Self>` |
| `fn from_iter<T: IntoIterator<Item = NodeSnapshot>>(..) -> Self` | `fn from_iter<E, I>(id, parent_id, message, properties, manifest_files, sorted_iter) -> IcechunkResult<Self>` where items are `Result<NodeSnapshot, E>` **and must already be sorted by `path`** |
| `fn from_fields(..)` | **REMOVED** |
| `fn adopt(&self, parent: &Snapshot) -> Self` | `fn adopt(&self, new_child: &Snapshot) -> IcechunkResult<Self>` — **the roles are swapped**: now `parent.adopt(&child)` |
| `impl From<&Snapshot> for SnapshotInfo` | `impl TryFrom<&Snapshot> for SnapshotInfo` |
| `type SnapshotProperties = HashMap<String, Value>` | `= BTreeMap<String, Value>` (iteration is sorted) |
| `struct Snapshot { id, parent_id, flushed_at, message, metadata, manifest_files, attribute_files, nodes }` | `struct Snapshot { /* one private byte buffer */ }` — move bytes with `bytes()` / `from_buffer()` |

`NodeSnapshot` and `NodeData`:

```rust
// previous milestone
pub struct NodeSnapshot { pub id: NodeId, pub path: Path,
                          pub user_attributes: Option<UserAttributesSnapshot>,
                          pub node_data: NodeData }
pub enum NodeData { Array(ZarrArrayMetadata, Vec<ManifestRef>), Group }
// this milestone
pub struct NodeSnapshot { pub id: NodeId, pub path: Path,
                          pub user_data: bytes::Bytes, pub node_data: NodeData }
pub enum NodeData { Array { shape: ArrayShape,
                            dimension_names: Option<Vec<DimensionName>>,
                            manifests: Vec<ManifestRef> }, Group }
```

Pattern migration: `NodeData::Array(..)` → `NodeData::Array { .. }`;
`node.user_attributes` → `node.user_data`.

### 7.4 `format::manifest::Manifest`

| Previous milestone | This milestone |
| --- | --- |
| `pub id: ManifestId` (a public **field**) | `pub fn id(&self) -> ManifestId` (a **method**, by value) |
| `fn new(chunks: BTreeMap<NodeId, BTreeMap<ChunkIndices, ChunkPayload>>) -> Self` | **REMOVED** — build with `from_stream` / `from_iter` |
| `impl Default for Manifest` | **REMOVED** |
| `#[derive(Debug, PartialEq)]` | `#[derive(Debug)]` only — `Manifest` is no longer `PartialEq` |
| `fn get_chunk_payload(..) -> IcechunkResult<&ChunkPayload>` | `-> IcechunkResult<ChunkPayload>` (by value) |
| `fn iter(self: Arc<Self>, node) -> impl Iterator<Item = (ChunkIndices, ChunkPayload)>` | items are now `Result<(ChunkIndices, ChunkPayload), IcechunkFormatError>` |
| `fn chunk_payloads(&self) -> impl Iterator<Item = &ChunkPayload>` | `-> impl Iterator<Item = Result<ChunkPayload, IcechunkFormatError>> + '_` |
| an in-memory `BTreeMap` representation | one private byte buffer; there is no map representation at all |
| `ManifestExtents` had a public tuple field | the field is private; construct with `ManifestExtents::new(&from, &to)`, read with `.iter()` |

### 7.5 `format::transaction_log::TransactionLog`

```rust
// previous milestone - derived deletions by diffing two node iterators, carried no id
pub fn new<'a>(cs: &ChangeSet,
               parent_nodes: impl Iterator<Item = &'a NodeSnapshot>,
               child_nodes:  impl Iterator<Item = &'a NodeSnapshot>) -> Self;
// this milestone - takes the snapshot id and reads deletions off the ChangeSet
pub fn new(id: &SnapshotId, cs: &ChangeSet) -> Self;
```

Every content accessor in section 4.6 is new; the previous milestone had only
`len`/`is_empty`. The struct's fields are now one private byte buffer.

### 7.6 `Session`, `Repository`, `storage`, `refs`, `store`, `change_set`

| Previous milestone | This milestone |
| --- | --- |
| `Session::add_group(&mut self, path: Path)` | `add_group(&mut self, path: Path, definition: Bytes)` |
| `Session::add_array(&mut self, path: Path, metadata: ZarrArrayMetadata)` | `add_array(&mut self, path: Path, shape: ArrayShape, dimension_names: Option<Vec<DimensionName>>, user_data: Bytes)` |
| `Session::update_array(&mut self, path: Path, metadata: ZarrArrayMetadata)` | `update_array(&mut self, path: &Path, shape, dimension_names, user_data)` — note `path` is now `&Path` |
| — | `Session::update_group(&mut self, path: &Path, definition: Bytes)` (NEW) |
| — | `Session::status(&self) -> SessionResult<Diff>` (NEW) |
| `Session::list_nodes(&self) -> SessionResult<impl Iterator<Item = NodeSnapshot> + '_>` | items are now `SessionResult<NodeSnapshot>` |
| `Repository::save_config(&self) -> RepositoryResult<ETag>` (`ETag = String`) | `-> RepositoryResult<storage::VersionInfo>` |
| `Repository::fetch_config(..) -> RepositoryResult<Option<(RepositoryConfig, ETag)>>` | `-> RepositoryResult<Option<(RepositoryConfig, storage::VersionInfo)>>` |
| — | `Repository::diff(&self, from: &VersionInfo, to: &VersionInfo) -> SessionResult<Diff>` (NEW; note the **`SessionResult`** return) |
| `pub fn new_in_memory_storage()` | `pub async fn new_in_memory_storage()` — **await it** |
| `pub fn new_local_filesystem_storage(path)` | `pub async fn new_local_filesystem_storage(path)` — await it |
| `pub fn new_gcs_storage(..)`, `pub fn new_azure_blob_storage(..)` | both `async` |
| `ObjectStorage::new_in_memory()`, `ObjectStorage::new_local_filesystem(&p)` | both `async` |
| `pub fn new_s3_storage(..)` | unchanged, still **sync** |
| — | `pub async fn new_s3_object_store_storage(..)` (NEW) |
| `storage::ETag = String` (alias) | `pub struct ETag(pub String)` (newtype) |
| `Storage::fetch_config -> StorageResult<Option<(Bytes, ETag)>>` | `-> StorageResult<FetchConfigResult>` |
| `Storage::update_config(..)` | gains `previous_version: &VersionInfo`, returns `UpdateConfigResult` |
| — | `Storage::get_ref` / `write_ref` returning `GetRefResult` / `WriteRefResult` (NEW) |
| `refs::create_tag(storage, settings, name, snapshot, overwrite_refs: bool)` | `create_tag(storage, settings, name, snapshot)` — **`overwrite_refs` dropped** |
| `refs::update_branch(.., overwrite_refs: bool) -> RefResult<BranchVersion>` | `update_branch(storage, settings, name, new_snapshot, current_snapshot) -> RefResult<()>` |
| branch files at `refs/branch.$NAME/$CROCKFORD_SEQ.json` | a single `refs/branch.$NAME/ref.json`, written with a conditional put |
| `Store::set_if_not_exists(&self, key, value: Bytes)` | the parameter is named `bytes` (call sites unaffected) |
| — | `Store::set_virtual_refs`, `Store::getsize_prefix`, `store::SetVirtualRefsResult` (NEW) |
| `change_set::add_group(path, node_id)` | `add_group(path, node_id, definition: Bytes)` |
| `change_set::add_array(path, node_id, ZarrArrayMetadata)` | `add_array(path, node_id, array_data: ArrayData)`, where `pub struct ArrayData { pub shape, pub dimension_names, pub user_data: Bytes }` is NEW |
| `change_set::get_array -> Option<&(NodeId, ZarrArrayMetadata)>` | `-> Option<&(NodeId, ArrayData)>` |
| `change_set::get_group -> Option<&NodeId>` | `-> Option<&(NodeId, Bytes)>` |
| `change_set::update_array(node_id, metadata)` | `update_array(&node_id, &path, array_data)` |
| `change_set::get_updated_zarr_metadata` | `get_updated_array` / `get_updated_group` |
| `change_set::zarr_updated_arrays` | `updated_arrays` (plus a new `updated_groups`) |
| `change_set::undo_user_attributes_update` | `undo_update` |
| `change_set::update_existing_chunks<'a>` | gains a type parameter `E` |

### 7.7 Dependency-visible changes

- `serde_yml 0.0.12` → `serde_yaml_ng` (leaks through
  `RepositoryErrorKind::ConfigDeserializationError`).
- New crates on the public surface: `flatbuffers`
  (`IcechunkFormatErrorKind::InvalidFlatBuffer`, the whole `format::flatbuffers`
  module), `tracing-error` (`ICError::context: SpanTrace`), `tracing`,
  `err-into`.
- `zstd` compresses the metadata payload but is not re-exported.

---

## 8. Formats and error categories crossing the boundary

- **Metadata objects** are the byte layout of section 5. The drivers read and
  write them through the `Storage` trait and assert the header bytes exactly.
- **Zarr metadata** is the caller's own bytes, stored verbatim as
  `NodeSnapshot::user_data` (section 4.12). Its JSON is never re-serialised by
  Icechunk, so no field ordering or whitespace question arises.
- **Reference files** are JSON: an object with a single key `"snapshot"` whose
  value is the Base 32 Crockford spelling of the 12-byte snapshot id. Branches
  live at `refs/branch.$NAME/ref.json`, tags at `refs/tag.$NAME/ref.json`.
- **Errors** are asserted by **category** — the alias's `kind` field matched
  against a named variant of the corresponding `…ErrorKind`, sometimes with a
  named field's value (`InvalidFileType { expected, got }`). The drivers never
  assert an error's `Display` text, its `Debug` rendering, or its source chain
  beyond `source().is_some()`.
- **Ordering**: where section 4 or section 6 states an order, it is asserted
  (snapshot nodes by path, manifest refs by coordinate, `SnapshotProperties`
  keys sorted, `list_branches`/`list_tags` as `BTreeSet`s, ancestry newest
  first). Any other iteration order is not asserted.

---

## 10. Out of scope at this milestone

Out of scope at every milestone:

- Internal module layout below the paths section 4 names, private type and
  function names, and file organisation under `icechunk/src/`.
- Error message wording, the **text** `Debug`/`Display` produce, and the
  contents of `ICError::context`. Whether those impls exist is required by
  sections 3 and 4; only what they print is out of scope.
- Iteration order this document and the corpus do not fix, allocation strategy,
  timing, memory use, throughput, compression level, and the FlatBuffers
  verifier limits.
- Your own tests, benchmarks, examples, documentation and CI configuration.
- Crate and package version strings, and the **value** of the implementation id
  written into header bytes 12–35. The header's *structure* is in scope — the
  field is 24 bytes of non-empty printable ASCII, right-padded, and a writer
  uses the same one for every object it writes (section 5.1) — but what that
  identification spells is not.
- Object-store object metadata (the `ic_spec_ver`, `ic_client`, `ic_file_type`,
  `ic_comp_alg` keys). Icechunk writes them and does not read them.

Out of scope at **this** milestone specifically:

- **The Python bindings.** `icechunk-python/` is never built or checked. Two
  0.2.0 release-note items are Python-wrapper-only and are **out of scope**,
  because the Rust signatures are byte-identical to the previous checkpoint:
  - *"`ancestry` is now a lazy, interruptible iterator."* In Rust,
    `Repository::ancestry`, `ancestry_arc`, `snapshot_ancestry` and
    `snapshot_ancestry_arc` were **already** lazy `futures::Stream`s at the
    previous checkpoint and their signatures did not change here. What changed
    is `icechunk.Repository.ancestry` returning a Python `Iterator` instead of a
    `list`. The drivers do assert the Rust-observable part — that a consumer may
    take a prefix of the stream and drop the rest — and nothing else.
  - *"`readonly_session` accepts a branch name as its first positional
    argument."* In Rust, `readonly_session(&self, version: &VersionInfo)` and
    `VersionInfo` are unchanged from the previous checkpoint; the positional
    argument is a Python keyword-argument change. A Rust test expresses
    "read-only session from a branch name" as
    `repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string()))`,
    and that is what the drivers do.
  - Likewise `VirtualChunkSpec` and the Python `Store.set_virtual_refs`
    signature are wrapper types. The in-scope bulk setter is
    **`icechunk::Store::set_virtual_refs`** (section 4.12) — on `Store`, not on
    `Session`.
- **Cloud and network backends.** S3, GCS, Azure and Tigris appear in this
  contract as configuration surfaces. No requirement group opens a network
  connection, reaches MinIO, Azurite or a public bucket, or needs credentials.
  Every group runs on the in-memory object store or a `tempfile::TempDir` local
  store, and virtual chunk references use `file://` URLs.
- **`attributes/` files.** The spec says their on-disk format is not defined and
  they are not implemented; `AttributeFileInfo` was removed. Nothing is in scope on
  them beyond `FileTypeBin::Attributes` keeping the code `3`.
- **The `logs` cargo feature** and `initialize_tracing()`; the `ICECHUNK_LOG`
  environment variable and anything it prints.
- **Benchmarks** (`icechunk/examples/`, the benchmark suite) and the
  performance claims in `design/008-no-copy-serialization-formats.md`: size,
  read speed and write speed against the previous format are measurements taken
  by the upstream authors, not requirements this milestone is checked on.
- **A migration path from the previous on-disk format.** There is none, by
  design; section 5.4 defines the rejection instead.
