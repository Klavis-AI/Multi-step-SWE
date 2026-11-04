# TEST_INTERFACE.md — milestone 08, backends, reliability and forward compatibility

This is the complete public contract the verifier holds this milestone to. It is
self-contained: everything an earlier milestone established and this milestone
still relies on is restated here in the spelling **this release** uses, so
nothing has to be recovered from an earlier step's copy of this file.

The normative prose lives in `/app/icechunk-spec/` (see its `README.md` for the
map and its `PROFILE.md` for the resolutions this milestone adopts where the
shipped documents disagree with each other or with the code). Where this
document and `/app/icechunk-spec/` disagree, **this document wins**, because it
is what the verifier was written against.

---

## 1. Workspace, crate identity and build

| | |
| --- | --- |
| workspace root | `/app/Cargo.toml`, `resolver = "2"` |
| members | `icechunk`, `icechunk-python`, `icechunk-macros` |
| default member | `icechunk` (so a bare `cargo build` at `/app` builds the library) |
| library crate | `icechunk`, at `/app/icechunk`, **version `0.3.24`**, `edition = "2024"` |
| licence | `Apache-2.0` |
| lockfile | `/app/Cargo.lock`, committed, must stay resolvable |
| features | no `default` feature. Optional: `logs`, `cli` |
| binary | `[[bin]] name = "icechunk"`, `required-features = ["cli"]` |

The verifier runs, in this order:

```
cargo build --manifest-path /app/Cargo.toml
cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk
```

and then compiles its own driver crate **outside `/app`**, depending on the
candidate by path (`icechunk = { path = "/app/icechunk" }`) with **no features
enabled**. Consequences the candidate must respect:

* The library must compile and be usable with **no feature flags on**.
* `icechunk::cli` must exist as a module at all times; its contents are behind
  `#[cfg(feature = "cli")]`.
* The built command-line binary is handed to the drivers through the
  environment variable **`ICECHUNK_TEST_CLI_BIN`**, holding an absolute path.

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
them. **Nothing this milestone checks requires you to add, remove or
re-pin a dependency**, so (2) is the ordinary path for a workspace that leaves
the manifests as it found them, and taking it costs a small fraction of the
verifier's resolution budget — your own `cargo build` has already populated the
registry cache and the shared build directory by the time it runs, and the
verifier's network is available for that one resolver run.

What (2) needs from you is only this: `/app/Cargo.lock` must stay a **regular,
committed file**, and cargo must be the thing that maintains it. Do not edit it
by hand, do not delete it, and do not replace it with a symbolic link — a
missing lockfile, or one that is not a regular file, makes the verifier skip (2)
altogether. A manifest that no resolution can satisfy costs every group at once.

Dependency types that cross the public API, at the versions the drivers link
directly: `bytes` 1, `chrono` 0.4, `futures` 0.3, `serde_json` 1, `tempfile` 3,
`tokio` 1. (`serde` itself is reached only through `serde_json`; the drivers
never name it, so a `Serialize`/`Deserialize` bound is exercised through
`serde_json::{to_string, from_str}` alone.) The drivers use `#[tokio::test]`
(current-thread runtime), so every `async fn` on the public surface must be
drivable from one.

### Object-store key layout

A repository occupies one object-store prefix. Directly under it:

| key | holds |
| --- | --- |
| `config.yaml` | the persisted `RepositoryConfig`, written only when the creating configuration differs from the default |
| `refs/…` | branch and tag references, including tag delete markers |
| `snapshots/<snapshot id>` | one snapshot object |
| `manifests/<manifest id>` | one chunk manifest |
| `transactions/<snapshot id>` | one transaction log |
| `chunks/<chunk id>` | one materialised chunk |
| **`repo`** | **reserved**: see §4 |

Object ids render through `Display` as the strings used in those keys.

---

## 2. What is new at this milestone

1. `RepositoryConfig::merge` now **replaces** `virtual_chunk_containers` instead
   of unioning them, and the struct gained `max_concurrent_requests` (§3).
2. A repository written by a newer on-disk format generation is refused, both
   on open and on commit (§4), resting on a new `Storage` existence probe.
3. `Session::get_chunk_ref` is the Rust driver for a chunk's four states, and
   `Session::all_node_chunks` walks one node's whole set of references (§5).
4. Storage statistics report native, virtual and inline bytes separately (§6).
5. Rebase tolerates a delete followed by a recreate at the same path (§7).
6. `Session::flush`, and `Repository::reset_branch`'s conditional third
   argument (§8).
7. Anonymous credentials for S3 and GCS are a supported, persistable
   configuration (§9).
8. The retry and stalled-stream **configuration surface** (§10).
9. The new `icechunk::inspect` module, and `Snapshot::get_node_index` beside it
   (§11).
10. The snapshot-node cache default rose to 500,000 (§3.4), the observable
    consequence of the raised on-disk size budgets.

---

## 3. Configuration — `icechunk::config`

`icechunk::{ObjectStoreConfig, RepositoryConfig}` are re-exported at the crate
root; everything else in this section is reached as `icechunk::config::…`.

```rust
pub struct RepositoryConfig {
    pub inline_chunk_threshold_bytes: Option<u16>,
    pub get_partial_values_concurrency: Option<u16>,
    pub compression: Option<CompressionConfig>,
    pub max_concurrent_requests: Option<u16>,      // NEW at this release
    pub caching: Option<CachingConfig>,
    pub storage: Option<icechunk::storage::Settings>,
    pub virtual_chunk_containers: Option<HashMap<String, VirtualChunkContainer>>,
    pub manifest: Option<ManifestConfig>,
}
```

`RepositoryConfig` derives `Clone + Debug + Serialize + Deserialize + PartialEq
+ Eq + Default`; `Default` is every field `None`.

Accessors, each returning the field's value or this release's default:

| accessor | default |
| --- | --- |
| `inline_chunk_threshold_bytes(&self) -> u16` | 512 |
| `get_partial_values_concurrency(&self) -> u16` | 10 |
| `max_concurrent_requests(&self) -> u16` | `DEFAULT_MAX_CONCURRENT_REQUESTS` |
| `compression(&self) -> &CompressionConfig` | `CompressionConfig::default()` |
| `caching(&self) -> &CachingConfig` | `CachingConfig::default()` |
| `manifest(&self) -> &ManifestConfig` | `ManifestConfig::default()` |
| `storage(&self) -> Option<&icechunk::storage::Settings>` | `None` (no default substituted) |

```rust
pub const DEFAULT_MAX_CONCURRENT_REQUESTS: u16 = 256;
```

`CompressionConfig` is unchanged at this release, but the verifier writes it as
a **full struct literal**, exactly as it does `S3Options` (§3.5), so both fields
must be present and public and spelled as below:

```rust
#[serde(rename_all = "snake_case")]
pub enum CompressionAlgorithm { Zstd }          // Default
pub struct CompressionConfig {
    pub algorithm: Option<CompressionAlgorithm>,
    pub level: Option<u8>,
}
```

Both derive `Clone + Debug + Serialize + Deserialize + PartialEq + Eq +
Default`; `CompressionAlgorithm` is additionally `Copy`. `CompressionConfig` has
the per-sub-field `merge` §3.1 refers to.

Virtual-chunk-container helpers, all pre-existing:

```rust
pub fn set_virtual_chunk_container(&mut self, cont: VirtualChunkContainer);
pub fn get_virtual_chunk_container(&self, url_prefix: &str) -> Option<&VirtualChunkContainer>;
pub fn virtual_chunk_containers(&self) -> impl Iterator<Item = &VirtualChunkContainer>;
pub fn clear_virtual_chunk_containers(&mut self);  // sets the field to Some(empty map)
```

`set_virtual_chunk_container` keys the map by `cont.url_prefix()`.
`clear_virtual_chunk_containers` sets the field to `Some(HashMap::new())` — an
*explicitly empty* set, which is not the same as `None` (unset).

### 3.1 `RepositoryConfig::merge` — precedence

```rust
pub fn merge(&self, other: Self) -> Self;
```

The signature is unchanged from the previous milestone. `self` is the **base**
("mine"); the argument is the **overlay** ("theirs"); the overlay wins.

For every field: `(None, None) => None`, `(None, Some(t)) => Some(t)`,
`(Some(m), None) => Some(m.clone())`. When both sides are `Some`:

| field | both `Some` |
| --- | --- |
| `inline_chunk_threshold_bytes` | overlay |
| `get_partial_values_concurrency` | overlay |
| `compression` | recursive `CompressionConfig::merge` |
| `max_concurrent_requests` | overlay (whole value) |
| `caching` | recursive `CachingConfig::merge` |
| `storage` | recursive `Settings::merge`, which recurses again into `ConcurrencySettings::merge` and `RetriesSettings::merge` |
| `virtual_chunk_containers` | **overlay, replacing the base's whole map** |
| `manifest` | recursive `ManifestConfig::merge` |

Every recursive `merge` is per-sub-field `other.or(self)`.

### 3.2 Virtual chunk containers replace on merge

This is the behaviour change at this release. Previously the two maps were
unioned; now the overlay's map replaces the base's outright. Two consequences
the verifier checks:

* merging a config whose containers were cleared with
  `clear_virtual_chunk_containers()` over a config that has containers yields
  `Some(HashMap::new())` — the clear is respected;
* merging a config holding container B over a config holding container A yields
  exactly `{B}`, never `{A, B}`.

The rule applies wherever `merge` is used, including
`Repository::reopen(Some(config), …)` and `Repository::open(Some(config), …)`.

### 3.3 `max_concurrent_requests`

`max_concurrent_requests` caps the number of object-store requests a repository
has in flight at once. The value is plumbed into the repository's asset manager
at construction; a repository configured with `Some(1)` must still function.
The field is part of the persisted configuration, so it survives
`Repository::create` followed by `Repository::open`.

### 3.4 `CachingConfig`

```rust
pub struct CachingConfig {
    pub num_snapshot_nodes: Option<u64>,
    pub num_chunk_refs: Option<u64>,
    pub num_transaction_changes: Option<u64>,
    pub num_bytes_attributes: Option<u64>,
    pub num_bytes_chunks: Option<u64>,
}
```

Accessor defaults at this release:

| accessor | default |
| --- | --- |
| `num_snapshot_nodes()` | **500_000** (was 30_000 at the previous milestone) |
| `num_chunk_refs()` | 15_000_000 |
| `num_transaction_changes()` | 0 |
| `num_bytes_attributes()` | 0 |
| `num_bytes_chunks()` | 0 |

`CachingConfig::merge(&self, other: Self) -> Self` is per-field `other.or(self)`.

### 3.5 `S3Options`

```rust
pub struct S3Options {
    pub region: Option<String>,
    pub endpoint_url: Option<String>,
    pub anonymous: bool,
    pub allow_http: bool,
    pub force_path_style: bool,                       // #[serde(default)] -> false
    pub network_stream_timeout_seconds: Option<u32>,  // NEW at this release
    pub requester_pays: bool,                         // NEW at this release, #[serde(default)]
}
```

Derives `Clone + Debug + Serialize + Deserialize + PartialEq + Eq`. Both new
fields are **breaking for every struct literal** in downstream code; the
verifier constructs `S3Options` as a full struct literal, so all seven fields
must be present, public, and spelled exactly as above.

`ObjectStoreConfig` is unchanged:

```rust
#[serde(rename_all = "snake_case")]
pub enum ObjectStoreConfig {
    InMemory,
    LocalFileSystem(PathBuf),
    Http(HashMap<String, String>),
    S3Compatible(S3Options),
    S3(S3Options),
    Gcs(HashMap<String, String>),
    Azure(HashMap<String, String>),   // a key in this map should be "account"
    Tigris(S3Options),
}
```

`ObjectStoreConfig` and `S3Options` must round-trip through `serde_json`
unchanged (`PartialEq`).

### 3.6 `ManifestConfig`

```rust
pub struct ManifestConfig {
    pub preload: Option<ManifestPreloadConfig>,
    pub splitting: Option<ManifestSplittingConfig>,
}
pub struct ManifestPreloadConfig {
    pub max_total_refs: Option<u32>,        // accessor default 10_000
    pub preload_if: Option<ManifestPreloadCondition>,
}
pub struct ManifestSplittingConfig {
    pub split_sizes: Option<Vec<(ManifestSplitCondition, Vec<ManifestSplitDim>)>>,
}
impl ManifestSplittingConfig { pub fn with_size(split_size: u32) -> Self; }
```

```rust
#[serde(rename_all = "snake_case")]
pub enum ManifestPreloadCondition {
    Or(Vec<ManifestPreloadCondition>),
    And(Vec<ManifestPreloadCondition>),
    PathMatches { regex: String },
    NameMatches { regex: String },
    NumRefs { from: Bound<u32>, to: Bound<u32> },
    True,
    False,
}
```

`ManifestConfig::{preload, splitting}()` return references, substituting the
default when the field is unset; `ManifestPreloadConfig::{max_total_refs,
preload_if}()` do the same for their fields. `ManifestSplittingConfig::with_size(n)` means
"every array, cut every dimension into runs of `n` chunks". The unset default is
one manifest per array. All of these round-trip through the persisted
configuration.

---

## 4. Forward compatibility — a newer on-disk format generation

### 4.1 The guard

```rust
// icechunk::repository::RepositoryErrorKind
#[error("this repository uses Icechunk v2 format, please upgrade the icechunk library")]
RepositoryIsV2,
```

`RepositoryErrorKind` is `#[non_exhaustive]`. It is surfaced as
`pub type RepositoryError = ICError<RepositoryErrorKind>`, where
`pub struct ICError<E> { pub kind: E, pub context: SpanTrace }` — so the
verifier always matches on **`err.kind`**, never on the rendered message.

**What triggers it: the mere existence of an object at the key `repo`** directly
under the repository prefix, a sibling of `config.yaml`, `refs/`, `snapshots/`,
`manifests/`, `transactions/` and `chunks/`. **Its contents are never read and
never parsed.** A zero-length object triggers it; so does arbitrary binary.

`Repository::open` performs this check **before anything else**, ahead of
fetching the configuration and ahead of deciding whether the repository exists
at all. A prefix that holds only a `repo` object therefore reports
`RepositoryIsV2`, not `RepositoryDoesntExist`.

`Repository::create` does *not* look for `repo` specifically, but it does refuse
to create into a prefix that already holds any object, with
`RepositoryErrorKind::ParentDirectoryNotClean`.

This is the only way an external caller can reach the guard: there is no
`Storage` write primitive on the public surface, so the object is created by
writing it directly into the store (for a local-filesystem repository,
`std::fs::write(dir.join("repo"), …)`).

### 4.2 The same guard on commit

`Session::commit` checks for the `repo` object as part of committing. The check
runs **after the snapshot and manifests have been flushed and before the branch
reference is updated**, so a repository migrated while a session is open fails
its commit with:

```rust
SessionErrorKind::RepositoryError(RepositoryErrorKind::RepositoryIsV2)
```

and **the branch is left exactly where it was**. The session's own
`snapshot_id()` is likewise unchanged. Objects written by the aborted flush are
left behind as garbage; that is expected and is out of scope.

Reads are not guarded: a `Repository` handle obtained before the object
appeared, and sessions created from it, keep working.

### 4.3 The storage primitive

The trait lives in `icechunk::storage` and is **re-exported at the crate root**,
so `icechunk::Storage` and `icechunk::storage::Storage` name the same item; the
drivers import it as `icechunk::Storage`. The root re-export is pre-existing and
carries `ObjectStorage`, `StorageError`, `new_in_memory_storage`,
`new_local_filesystem_storage` and `new_s3_storage` alongside it.

```rust
pub trait Storage: fmt::Debug + fmt::Display + Sealed + Sync + Send {
    async fn has_object(&self, settings: &Settings, key: &str) -> StorageResult<bool>;   // NEW, required
    async fn has_v2_repo_info(&self) -> StorageResult<bool>;                             // NEW, provided
    fn default_settings(&self) -> Settings;
    fn can_write(&self) -> bool;
    // … the pre-existing methods …
}
```

`has_object` answers for **one key relative to the repository prefix**: `true`
when an object is there, `false` when it is not — a missing object is not an
error. `has_v2_repo_info()` is exactly `has_object(&self.default_settings(),
"repo")`.

The trait is sealed, so only the crate's own implementations exist; the verifier
reaches these methods through `Arc<dyn Storage + Send + Sync>` obtained from
`icechunk::new_in_memory_storage()` or
`icechunk::new_local_filesystem_storage(&std::path::Path)`. Both return
`StorageResult<Arc<dyn Storage>>`.

### 4.4 The per-file spec-version guard (pre-existing, still in force)

Every snapshot, manifest and transaction-log object begins with a 39-byte
header:

| offset | length | content |
| --- | --- | --- |
| 0 | 12 | `icechunk::format::format_constants::ICECHUNK_FORMAT_MAGIC_BYTES`, i.e. `"ICE🧊CHUNK"` in UTF-8 |
| 12 | 24 | the writing client's name, left-aligned and padded to 24 bytes; **ignored on read** |
| **36** | **1** | **spec version**, which must equal `SpecVersionBin::current() as u8 == 1` |
| 37 | 1 | file type: `FileTypeBin::{Snapshot = 1, Manifest = 2, Attributes = 3, TransactionLog = 4, Chunk = 5}` |
| 38 | 1 | compression: `CompressionAlgorithmBin::{None = 0, Zstd = 1}` |
| 39 | … | the compressed payload |

`icechunk::format::format_constants` is public and exports
`ICECHUNK_FORMAT_MAGIC_BYTES`, `SpecVersionBin`, `FileTypeBin` and
`CompressionAlgorithmBin` with those discriminants.

Reading an object whose byte 36 is anything but 1 fails with
`RepositoryErrorKind::FormatError(IcechunkFormatErrorKind::InvalidSpecVersion)`;
a corrupt magic prefix fails with
`…FormatError(IcechunkFormatErrorKind::InvalidMagicNumbers)`. Both reach the
caller from any operation that parses the object — `Repository::lookup_snapshot`
is the one the verifier uses. `IcechunkFormatErrorKind` lives in
`icechunk::format`.

---

## 5. Chunk reference states — `Session::get_chunk_ref`

```rust
// icechunk::session::Session
pub async fn get_chunk_ref(
    &self,
    path: &Path,
    coords: &ChunkIndices,
) -> SessionResult<Option<ChunkPayload>>;

// icechunk::format::manifest
#[non_exhaustive]
pub enum ChunkPayload {
    Inline(Bytes),
    Virtual(VirtualChunkRef),
    Ref(ChunkRef),
}
pub struct ChunkRef { pub id: ChunkId, pub offset: ChunkOffset, pub length: ChunkLength }
pub struct VirtualChunkRef {
    pub location: VirtualChunkLocation,
    pub offset: ChunkOffset,
    pub length: ChunkLength,
    pub checksum: Option<Checksum>,
}
```

The result is the complete statement of a chunk's state:

| result | state |
| --- | --- |
| `Ok(None)` | **uninitialised** |
| `Ok(Some(ChunkPayload::Ref(_)))` | **native** — materialised in this repository's chunk storage |
| `Ok(Some(ChunkPayload::Inline(_)))` | **inline** — carried in the manifest itself |
| `Ok(Some(ChunkPayload::Virtual(_)))` | **virtual** — a reference to bytes in another store |

`Ok(None)` covers both uninitialised cases: coordinates outside the array's
chunk grid, and in-bounds coordinates with no reference set (including one that
was set and then cleared with `set_chunk_ref(path, coords, None)`). Asking a
**group** for a chunk reference is an error, not a state.

Which of `Ref` and `Inline` a written chunk becomes is decided by
`RepositoryConfig::inline_chunk_threshold_bytes()`: the chunk writer returned by
`Session::get_chunk_writer()` produces `Ref` when `data.len() > threshold` and
`Inline` otherwise.

**Every call to that writer materialises a new chunk under a freshly generated
`ChunkId`**, whatever the bytes are. Writing the same bytes twice therefore
stores two distinct chunks with two distinct ids, and overwriting a coordinate
that already held a native chunk leaves the old chunk in storage under its own
id. The only way two references name the same stored chunk is for a
`ChunkPayload::Ref` carrying an existing `ChunkId` to be re-used. §6.2 depends
on this.

```rust
pub fn get_chunk_writer(&self)
    -> impl FnOnce(Bytes) -> Pin<Box<dyn Future<Output = SessionResult<ChunkPayload>> + Send>>;
pub async fn set_chunk_ref(&mut self, path: Path, coord: ChunkIndices, data: Option<ChunkPayload>)
    -> SessionResult<()>;
```

`VirtualChunkLocation::from_absolute_path(&str) -> Result<Self, VirtualReferenceError>`
normalises a URL; `url()` reads it back. A virtual reference is **recorded, never
dereferenced**, by any operation the verifier performs.

One node's whole set of references is reachable at once:

```rust
// icechunk::session::Session
pub async fn all_node_chunks<'a>(&'a self, node: &'a NodeSnapshot)
    -> impl Stream<Item = SessionResult<(Path, ChunkInfo)>> + 'a;   // NEW at this release

// icechunk::format::manifest
pub struct ChunkInfo { pub node: NodeId, pub coord: ChunkIndices, pub payload: ChunkPayload }
```

It yields one item per chunk reference the node currently holds — committed and
uncommitted alike — each paired with that node's path, and with the same
`ChunkPayload` `get_chunk_ref` would return for those coordinates. The
`NodeSnapshot` argument is one of the values `Session::list_nodes` yields
(§12.2). Iteration order is not part of the contract.

The states survive a commit and are observable again through a read-only
session at that snapshot.

> The Python binding presents the same four states as an enum named
> `ChunkType` with members `UNINITIALIZED`, `NATIVE`, `VIRTUAL`, `INLINE`. That
> enum is **not** part of the Rust crate and is out of scope — see §14.

---

## 6. Storage statistics — `icechunk::ops::stats`

```rust
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct ChunkStorageStats {
    pub native_bytes: u64,
    pub virtual_bytes: u64,
    pub inlined_bytes: u64,
}

impl ChunkStorageStats {
    pub fn new(native_bytes: u64, virtual_bytes: u64, inlined_bytes: u64) -> Self;
    pub fn non_virtual_bytes(&self) -> u64;   // native + inlined, saturating
    pub fn total_bytes(&self) -> u64;         // native + virtual + inlined, saturating
}
impl std::ops::Add for ChunkStorageStats { type Output = Self; }  // saturating, per field

pub async fn repo_chunks_storage(
    storage: &(dyn Storage + Send + Sync),
    storage_settings: &icechunk::storage::Settings,
    asset_manager: Arc<AssetManager>,
    max_snapshots_in_memory: NonZeroU16,
    max_compressed_manifest_mem_bytes: NonZeroUsize,
    max_concurrent_manifest_fetches: NonZeroU16,
) -> RepositoryResult<ChunkStorageStats>;
```

`repo_chunks_storage` previously returned one `u64`; it now reports the three
totals separately. It walks every manifest reachable from every reference in the
repository. **Only committed state counts**: an uncommitted session contributes
nothing, so a repository with no commits reports `ChunkStorageStats::default()`.

### 6.1 The `AssetManager` the statistics take

```rust
// icechunk::asset_manager::AssetManager
pub fn new(storage, storage_settings, num_snapshot_nodes: u64, num_chunk_refs: u64,
           num_transaction_changes: u64, num_bytes_attributes: u64, num_bytes_chunks: u64,
           compression_level: u8, max_concurrent_requests: u16) -> Self;
pub fn new_no_cache(storage, storage_settings, compression_level: u8,
                    max_concurrent_requests: u16) -> Self;
pub fn new_with_config(storage, storage_settings, config: &CachingConfig,
                       compression_level: u8, max_concurrent_requests: u16) -> Self;
```

All three gained the trailing `max_concurrent_requests: u16` parameter at this
release. `Repository::asset_manager(&self) -> &Arc<AssetManager>` hands out the
repository's own.

### 6.2 Deduplication

| kind | counted as | deduplicated by |
| --- | --- | --- |
| native (`ChunkPayload::Ref`) | `chunk_ref.length` | the chunk's `ChunkId`, across every manifest |
| virtual (`ChunkPayload::Virtual`) | `virtual_ref.length` | the tuple `(location, offset, length)`, across every manifest |
| inline (`ChunkPayload::Inline`) | `bytes.len()` | **not deduplicated** |

The walk covers **every snapshot reachable from every reference, through its
whole ancestry**, and manifests are visited once per distinct manifest id. A
commit that touches one array rewrites that array's *whole* manifest under a new
id, so after such a commit both the old manifest and the new one are counted.

Inline bytes are counted once for **every occurrence in every manifest**,
because an inline chunk is physically re-stored in each manifest that carries
it. So a second commit over an array whose manifest holds ten one-byte inline
chunks takes the repository's `inlined_bytes` from 10 to 20, even though those
ten references were not touched: the rewritten manifest carries its own copy of
each of them.

**Deduplicated by `ChunkId` does not mean `native_bytes` cannot grow.** Because
every call to the chunk writer mints a fresh `ChunkId` (§5), rewriting ten
six-byte native chunks *adds* sixty bytes: the old manifest still references the
ten old ids and the new manifest references ten new ones, and all twenty are
distinct chunks. Deduplication collapses only repeated references to the **same**
`ChunkId` — the same stored chunk reached from more than one manifest. So over
the two commits just described, a repository holding fifty native chunks of six
bytes each, ten of which the second commit rewrites, reports
`native_bytes == (50 + 10) × 6 == 360`.

Two virtual references differing in any one of URL, offset or length are
distinct chunks.

---

## 7. Rebase — delete followed by recreate

```rust
// icechunk::session::Session
pub async fn rebase(&mut self, solver: &(dyn ConflictSolver + Send + Sync)) -> SessionResult<()>;

// icechunk::conflicts
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
pub mod detector { pub struct ConflictDetector; }
pub mod basic_solver {
    pub enum VersionSelection { Fail, UseOurs, UseTheirs }
    pub struct BasicConflictSolver {
        pub on_chunk_conflict: VersionSelection,        // Default: UseOurs
        pub fail_on_delete_of_updated_array: bool,      // Default: false
        pub fail_on_delete_of_updated_group: bool,      // Default: false
    }
}
```

`Conflict` derives `Debug + PartialEq + Eq`, so a test can assert membership in
the reported list.

**The change at this release.** A session that deletes a node and then creates a
new node at the same path is performing a replacement, not introducing a
colliding new node. `ConflictDetector` therefore no longer reports
`Conflict::NewNodeConflictsWithExistingNode` for a path that the same change set
also deletes. This holds for groups and for arrays.

What still conflicts is unchanged: if the concurrent commit **updated** the node
being deleted, the rebase fails with
`SessionErrorKind::RebaseFailed { snapshot, conflicts }` whose `conflicts`
contains `Conflict::DeleteOfUpdatedGroup { path, node_id }` (or
`DeleteOfUpdatedArray`) carrying the node's identity as it was before the
delete.

A commit whose branch moved underneath it fails with
`SessionErrorKind::Conflict { expected_parent, actual_parent }` *before* any
rebase is attempted; that is the signal to rebase.

---

## 8. `Session::flush` and conditional `Repository::reset_branch`

```rust
pub async fn flush(&mut self, message: &str, properties: Option<SnapshotProperties>)
    -> SessionResult<SnapshotId>;
```

`flush` writes a complete, durable snapshot — manifests, transaction log,
metadata — **without touching any branch reference**. On success it clears the
change set, repoints the session at the new snapshot, and **demotes the session
to read-only**: afterwards `branch()` is `None`, `read_only()` is `true`,
`changes()` is empty, `snapshot_id()` is the new id, and a further `commit`
fails with `SessionErrorKind::ReadOnlySession`. The repository's
`default_commit_metadata` is merged underneath `properties`, exactly as for
`commit`.

```rust
pub async fn reset_branch(
    &self,
    branch: &str,
    to_snapshot_id: &SnapshotId,
    from_snapshot_id: Option<&SnapshotId>,   // NEW third argument
) -> RepositoryResult<()>;
```

`from_snapshot_id` is the caller's expectation of where the branch is **now**.
`Some(id)` performs the move only if the branch is still at `id`, and errors
otherwise without changing anything. `None` looks the current tip up and moves
unconditionally, which is the previous milestone's behaviour.

Together these make a flushed snapshot permanent:
`Repository::{create_tag, create_branch, reset_branch}` all accept it.

`Snapshot::INITIAL_SNAPSHOT_ID` is the fixed id of every repository's first
snapshot and `Snapshot::INITIAL_COMMIT_MESSAGE` (`"Repository initialized"`) its
message.

---

## 9. Credentials

```rust
// icechunk::config
#[serde(tag = "credential_type", rename_all = "snake_case")]
pub enum Credentials { S3(S3Credentials), Gcs(GcsCredentials), Azure(AzureCredentials) }

#[serde(tag = "s3_credential_type", rename_all = "snake_case")]
pub enum S3Credentials { FromEnv /* Default */, Anonymous, Static(S3StaticCredentials),
                         Refreshable(Arc<dyn S3CredentialsFetcher>) }

#[serde(tag = "gcs_credential_type", rename_all = "snake_case")]
pub enum GcsCredentials { FromEnv /* Default */, Anonymous, Static(GcsStaticCredentials),
                          Refreshable(Arc<dyn GcsCredentialsFetcher>) }

#[serde(tag = "az_credential_type", rename_all = "snake_case")]
pub enum AzureCredentials { FromEnv, Static(AzureStaticCredentials) }
```

`Credentials`, `S3Credentials` and `GcsCredentials` derive
`Clone + Debug + Serialize + Deserialize` (`S3Credentials` and `GcsCredentials`
also `Default`, which is `FromEnv`). They do **not** derive `PartialEq`, so the
verifier matches on variants rather than comparing. `Credentials` values must
round-trip through `serde_json`.

`S3Credentials::Anonymous` and `GcsCredentials::Anonymous` are the anonymous
public-bucket credentials. S3 additionally carries the orthogonal
`S3Options { anonymous: bool }` flag, which controls request signing at the
object-store layer; the two are independent and both must exist.

`VirtualChunkContainer::validate_credentials(&self, cred: Option<&Credentials>)
-> Result<(), String>` accepts credentials of the store's own kind, accepts
`None` for S3, S3-compatible, GCS and Tigris containers, and rejects
credentials belonging to another backend.

Authorising a container for a repository:

```rust
Repository::create(config, storage, authorize_virtual_chunk_access: HashMap<String, Option<Credentials>>)
Repository::open  (config, storage, authorize_virtual_chunk_access: HashMap<String, Option<Credentials>>)
// `reopen` does no I/O, so unlike `create`/`open` it is NOT `async`:
Repository::reopen(&self, config: Option<RepositoryConfig>,
                   authorize_virtual_chunk_access: Option<HashMap<String, Option<Credentials>>>)
    -> RepositoryResult<Repository>
pub fn authorized_virtual_container_prefixes(&self) -> HashSet<String>;   // NEW at this release
```

`reopen` merges the configuration it is given over the repository's current one
(§3.1, including §3.2's replace rule) and keeps the current authorisation map
when the second argument is `None`.

`authorized_virtual_container_prefixes` returns the keys of the authorisation
map as they were supplied. A key naming a container whose credentials do not
validate makes `create`/`open`/`reopen` fail.

No operation the verifier performs contacts a bucket.

---

## 10. Retry and stalled-stream configuration

```rust
// icechunk::storage
pub struct RetriesSettings {
    pub max_tries: Option<NonZeroU16>,
    pub initial_backoff_ms: Option<u32>,
    pub max_backoff_ms: Option<u32>,
}
impl RetriesSettings {
    pub fn max_tries(&self) -> NonZeroU16;     // default 10
    pub fn initial_backoff_ms(&self) -> u32;   // default 100
    pub fn max_backoff_ms(&self) -> u32;       // default 180_000 (3 minutes)
    pub fn merge(&self, other: Self) -> Self;  // per field, other.or(self)
}

pub struct ConcurrencySettings {
    pub max_concurrent_requests_for_object: Option<NonZeroU16>,
    pub ideal_concurrent_request_size: Option<NonZeroU64>,
}
impl ConcurrencySettings {
    pub fn max_concurrent_requests_for_object(&self) -> NonZeroU16;  // default 18
    pub fn ideal_concurrent_request_size(&self) -> NonZeroU64;       // default 12 MiB
    pub fn merge(&self, other: Self) -> Self;   // per field, other.or(self)
}

pub struct Settings {
    pub concurrency: Option<ConcurrencySettings>,
    pub retries: Option<RetriesSettings>,
    pub unsafe_use_conditional_update: Option<bool>,   // default true
    pub unsafe_use_conditional_create: Option<bool>,   // default true
    pub unsafe_use_metadata: Option<bool>,             // default true
    pub storage_class: Option<String>,
    pub metadata_storage_class: Option<String>,
    pub chunks_storage_class: Option<String>,
    pub minimum_size_for_multipart_upload: Option<u64>, // default 100 MiB
}
impl Settings {
    pub fn concurrency(&self) -> &ConcurrencySettings;
    pub fn retries(&self) -> &RetriesSettings;
    pub fn unsafe_use_conditional_update(&self) -> bool;        // default true
    pub fn unsafe_use_conditional_create(&self) -> bool;        // default true
    pub fn unsafe_use_metadata(&self) -> bool;                  // default true
    pub fn metadata_storage_class(&self) -> Option<&String>;    // else `storage_class`
    pub fn chunks_storage_class(&self) -> Option<&String>;      // else `storage_class`
    pub fn minimum_size_for_multipart_upload(&self) -> u64;     // default 100 MiB
    pub fn merge(&self, other: Self) -> Self;   // recursive into the two sub-structs
}
```

Each accessor substitutes the documented default when its field is unset;
`metadata_storage_class()` and `chunks_storage_class()` fall back to
`storage_class` before giving up, so setting `storage_class` alone sets both.

`Settings::merge` follows §3.1's rule for every one of the nine fields: per
field, `other.or(self)`, recursing into `concurrency` and `retries` when both
sides are `Some`. All nine round-trip through `serde_json`.

All three derive `Debug + PartialEq + Eq + Serialize + Deserialize + Clone +
Default`.

`S3Options::network_stream_timeout_seconds` is the stalled-stream grace period
in seconds. `Some(0)` disables stall detection entirely; `None` means **this
release's own default of 10 seconds**. (`/app/icechunk-spec/guide/performance.md`
still says 60 seconds; see `PROFILE.md` §R8-1 — the code and the change log agree
on 10 and the documentation is stale.)

**Storage settings resolution.** `Repository::{create, open, open_or_create}`
resolve the effective storage settings as
`storage.default_settings().merge(user_config_storage)` — a user configuration
overrides **only the fields it sets** and leaves the backend's other defaults
in place. `Repository::storage_settings(&self) -> &Settings` exposes the result;
`Repository::config().storage()` still reports the user's own settings, and
those are what the repository persists.

**The retry *behaviour* is out of scope** — see §14.

---

## 11. `icechunk::inspect`

A new module, **not** re-exported at the crate root, exposing exactly one public
item:

```rust
pub async fn snapshot_json(
    asset_manager: &AssetManager,
    id: &SnapshotId,
    pretty: bool,
) -> RepositoryResult<String>;
```

The returned string is a JSON object with these members:

| key | type | notes |
| --- | --- | --- |
| `id` | string | the snapshot id as `Display` renders it |
| `parent_id` | string | **omitted** when the snapshot has no parent |
| `flushed_at` | string | RFC 3339 timestamp |
| `commit_message` | string | |
| `metadata` | object | the snapshot properties |
| `manifests` | array | `{ "id": string, "size_bytes": number, "num_chunk_refs": number }` |
| `nodes` | array | `{ "id": string, "path": string, "node_type": "group" \| "array", "manifest_refs"?: […] }` |

`node_type` is the literal string `"group"` or `"array"`. `manifest_refs` is
present for arrays (possibly an empty array) and **omitted** for groups; each
entry is `{ "id": string, "extents": [[number, number], …] }`.

`pretty = true` returns the same value indented over several lines;
`pretty = false` returns it on one line. Both must parse to the same JSON value.

The snapshot object the rendering is built from is reachable directly, through
the asset manager `Repository::asset_manager()` hands out:

```rust
// icechunk::asset_manager::AssetManager
pub async fn fetch_snapshot(&self, id: &SnapshotId) -> RepositoryResult<Arc<Snapshot>>;

// icechunk::format::snapshot::Snapshot
pub fn get_node_index(&self, path: &Path) -> Result<usize, IcechunkFormatError>;  // NEW
```

`get_node_index` reports the position of the node with that path within the
snapshot's own node list, so two distinct nodes have two distinct positions and
every position is below the number of nodes. A path the snapshot does not hold
is an **error**, not `None`; no particular error kind is required for it.
`IcechunkFormatError` is the `ICError<IcechunkFormatErrorKind>` alias §4.4 uses.
The node list's *order* is not part of the contract — neither here nor in the
`nodes` array above.

---

## 12. Inherited contract — milestones 01 to 07

Everything below is still required, in the spelling this release uses.

### 12.1 Repository, references and history (milestones 01–03)

```rust
Repository::create(Option<RepositoryConfig>, Arc<dyn Storage + Send + Sync>,
                   HashMap<String, Option<Credentials>>) -> RepositoryResult<Repository>
Repository::open(…same…)            Repository::open_or_create(…same…)
Repository::exists(&(dyn Storage + Send + Sync)) -> RepositoryResult<bool>
Repository::config(&self) -> &RepositoryConfig
Repository::storage(&self) -> &Arc<dyn Storage + Send + Sync>
Repository::list_branches(&self) -> RepositoryResult<BTreeSet<String>>
Repository::lookup_branch(&self, &str) -> RepositoryResult<SnapshotId>
Repository::create_branch(&self, &str, &SnapshotId) -> RepositoryResult<()>
Repository::delete_branch(&self, &str) -> RepositoryResult<()>
Repository::list_tags / lookup_tag / create_tag / delete_tag
Repository::lookup_snapshot(&self, &SnapshotId) -> RepositoryResult<SnapshotInfo>
Repository::ancestry(&self, &VersionInfo)
    -> RepositoryResult<impl Stream<Item = RepositoryResult<SnapshotInfo>>>
Repository::readonly_session(&self, &VersionInfo) -> RepositoryResult<Session>
Repository::writable_session(&self, branch: &str) -> RepositoryResult<Session>
Repository::default_commit_metadata(&self) -> &SnapshotProperties
Repository::set_default_commit_metadata(&mut self, metadata: SnapshotProperties)

pub enum VersionInfo { SnapshotId(SnapshotId), TagRef(String), BranchTipRef(String),
                       AsOf { branch: String, at: DateTime<Utc> } }
pub struct SnapshotInfo { pub id: SnapshotId, pub parent_id: Option<SnapshotId>,
                          pub flushed_at: DateTime<Utc>, pub message: String,
                          pub metadata: SnapshotProperties,
                          pub manifests: Vec<ManifestFileInfo> }
pub struct ManifestFileInfo { pub id: ManifestId, pub size_bytes: u64, pub num_chunk_refs: u32 }
pub type SnapshotProperties = BTreeMap<String, serde_json::Value>;
```

Required behaviour: a fresh repository has exactly the branch `main` pointing at
`Snapshot::INITIAL_SNAPSHOT_ID`, whose message is
`Snapshot::INITIAL_COMMIT_MESSAGE`. `open` on a prefix where nothing was ever
created fails with `RepositoryErrorKind::RepositoryDoesntExist`; a second
`create` over the same prefix fails. `main` cannot be deleted. A tag is
immutable, and a **deleted tag's name stays retired** — recreating it fails.
Ancestry is newest-first and carries each commit's message and metadata.

`set_default_commit_metadata` replaces the repository's default commit
metadata, which starts empty; a session takes a copy when it is created, so set
it **before** `writable_session`. Both `Session::commit` and `Session::flush`
merge that default **underneath** the properties they are given: a key the call
supplies wins, and a default key the call does not mention survives (§8).

### 12.2 Sessions, hierarchy and conflicts (milestones 01–02)

A `Session` is the unit of work. The surface the verifier drives:

```rust
// icechunk::format
pub struct ChunkIndices(pub Vec<u32>);
impl Path { pub fn root() -> Path; }
impl TryFrom<&str> for Path { type Error = PathError; }     // "/a/b" -> Path
pub enum ByteRange { /* … */ }                              // ByteRange::ALL

// icechunk::format::snapshot
impl ArrayShape {
    /// One `(array_length, chunk_length)` pair per dimension; `None` when a
    /// chunk length is zero.
    pub fn new<I: IntoIterator<Item = (u64, u64)>>(it: I) -> Option<ArrayShape>;
    pub fn valid_chunk_coord(&self, coord: &ChunkIndices) -> bool;
}

// icechunk::session::Session
pub async fn add_group(&mut self, path: Path, user_data: Bytes) -> SessionResult<()>;
pub async fn add_array(&mut self, path: Path, shape: ArrayShape,
                       dimension_names: Option<Vec<DimensionName>>,
                       user_data: Bytes) -> SessionResult<()>;
pub async fn update_group(&mut self, path: &Path, user_data: Bytes) -> SessionResult<()>;
pub async fn update_array(&mut self, path: &Path, shape: ArrayShape,
                          dimension_names: Option<Vec<DimensionName>>,
                          user_data: Bytes) -> SessionResult<()>;
pub async fn delete_group(&mut self, path: Path) -> SessionResult<()>;
pub async fn delete_array(&mut self, path: Path) -> SessionResult<()>;
pub async fn get_node(&self, path: &Path)  -> SessionResult<NodeSnapshot>;
pub async fn get_group(&self, path: &Path) -> SessionResult<NodeSnapshot>;
pub async fn get_array(&self, path: &Path) -> SessionResult<NodeSnapshot>;
pub async fn commit(&mut self, message: &str, properties: Option<SnapshotProperties>)
    -> SessionResult<SnapshotId>;
pub async fn get_chunk_reader(&self, path: &Path, coords: &ChunkIndices,
                              byte_range: &ByteRange) -> SessionResult<Option<…>>;
pub fn branch(&self) -> Option<&str>;
pub fn read_only(&self) -> bool;
pub fn snapshot_id(&self) -> &SnapshotId;
pub fn changes(&self) -> &ChangeSet;              // `is_empty()` after a commit or flush
pub fn has_uncommitted_changes(&self) -> bool;
pub fn config(&self) -> &RepositoryConfig;
pub fn matching_container(&self, loc: &VirtualChunkLocation) -> Option<&VirtualChunkContainer>;

// icechunk::session
pub async fn get_chunk(reader: Option<…>) -> SessionResult<Option<Bytes>>;
```

`NodeSnapshot` carries at least `id: NodeId` and `path: Path` publicly, and
`node_type() -> NodeType` (`Group` or `Array`). `get_chunk(session
.get_chunk_reader(path, coords, &ByteRange::ALL).await?).await?` is how the
verifier reads a chunk's bytes back.

Two writable sessions on one branch may both start; the second to commit fails
with `SessionErrorKind::Conflict { expected_parent, actual_parent }` and must
rebase. `BasicConflictSolver { on_chunk_conflict: VersionSelection::UseOurs, .. }`
resolves a chunk double-update in favour of the rebasing session;
`ConflictDetector` alone reports it as unsolvable.

`Session::list_nodes` takes the parent group to list from:

```rust
pub async fn list_nodes<'a>(&'a self, parent_group: &Path)
    -> SessionResult<impl Iterator<Item = SessionResult<NodeSnapshot>> + use<'a>>;
```

Pass `&Path::root()` for the whole repository; the iterator yields the
`parent_group` node itself as well as its descendants.

### 12.3 Expiration and garbage collection (milestones 03 and 05)

```rust
// icechunk::ops::gc
pub enum Action { Keep, DeleteIfCreatedBefore(DateTime<Utc>) }
pub struct GCSummary { pub bytes_deleted: u64, pub chunks_deleted: u64,
                       pub manifests_deleted: u64, pub snapshots_deleted: u64,
                       pub attributes_deleted: u64, pub transaction_logs_deleted: u64 }
impl GCConfig {
    pub fn new(extra_roots: HashSet<SnapshotId>,
               dangling_chunks: Action, dangling_manifests: Action,
               dangling_attributes: Action, dangling_transaction_logs: Action,
               dangling_snapshots: Action,
               max_snapshots_in_memory: NonZeroU16,
               max_compressed_manifest_mem_bytes: NonZeroUsize,
               max_concurrent_manifest_fetches: NonZeroU16,
               dry_run: bool) -> Self;
    pub fn clean_all(chunks_age: DateTime<Utc>, metadata_age: DateTime<Utc>,
                     extra_roots: Option<HashSet<SnapshotId>>,
                     max_snapshots_in_memory: NonZeroU16,
                     max_compressed_manifest_mem_bytes: NonZeroUsize,
                     max_concurrent_manifest_fetches: NonZeroU16,
                     dry_run: bool) -> Self;
}
pub async fn garbage_collect(storage, storage_settings, asset_manager: Arc<AssetManager>,
                             config: &GCConfig) -> GCResult<GCSummary>;
pub async fn expire(storage, storage_settings, asset_manager: Arc<AssetManager>,
                    older_than: DateTime<Utc>, expired_branches: ExpiredRefAction,
                    expired_tags: ExpiredRefAction) -> GCResult<ExpireResult>;
pub enum ExpiredRefAction { Delete, Ignore }
pub struct ExpireResult { pub released_snapshots: HashSet<SnapshotId>,
                          pub edited_snapshots: HashSet<SnapshotId>,
                          pub deleted_refs: HashSet<Ref> }
```

`clean_all` is `new` with `Action::DeleteIfCreatedBefore` in all five slots;
`Action::Keep` in a slot leaves that class of object alone, and a configuration
whose five slots are all `Keep` deletes nothing at all and returns
`GCSummary::default()`, however much is dangling.

`ExpiredRefAction::Delete` deletes a reference whose whole ancestry expired;
`ExpiredRefAction::Ignore` leaves it alone, so `ExpireResult::deleted_refs` is
then empty. The default branch `main` is never deleted by expiration whichever
action is passed. `ExpireResult` derives `Debug + PartialEq + Eq + Clone +
Default`.

`GCSummary` derives `Debug + PartialEq + Eq + Default`. Garbage collection over
a repository with nothing dangling returns `GCSummary::default()` and deletes
nothing. After a branch is reset away from a commit, that commit's snapshot,
transaction log, manifest and any chunk only it referenced become collectable,
and the surviving history still reads.

Garbage collection and expiration are separate operations. `expire` rewrites
each reference's *history*, never its tip: snapshots flushed before `older_than`
stop being reachable and are reported in `released_snapshots`; the oldest
snapshot that survives is rewritten to descend directly from the repository's
root snapshot and is reported in `edited_snapshots`; and no reference is moved,
whichever `ExpiredRefAction` is passed. Removing the released objects is
garbage collection's job, not expiration's.

### 12.4 The command-line interface (milestone 05)

The binary is `icechunk`, built with `--features cli`, and the verifier invokes
it through **`ICECHUNK_TEST_CLI_BIN`**.

| invocation | effect | exit |
| --- | --- | --- |
| `icechunk --help` | usage listing the subcommands `repo`, `snapshot`, `config` | 0 |
| `icechunk repo create <alias>` | creates the repository named by `<alias>` in the configuration file | 0 |
| `icechunk snapshot list <alias>` | prints the branch's ancestry, **one line per snapshot**, newest first, at most `-n` (default 10) lines, from branch `-b` (default `main`) | 0 |
| `icechunk config list` | prints the current configuration | 0 |
| any failure (unknown alias, missing configuration, failed operation) | a diagnostic on stderr | non-zero |
| an unknown subcommand, or a missing required argument | a clap usage error on stderr | non-zero |

Each line `snapshot list` prints **identifies its own snapshot**: the snapshot's
id, spelled as `SnapshotId` renders through `Display`, appears somewhere in that
snapshot's line. Nothing else about a line's layout is fixed, and no message
wording is in scope. `config list` prints the whole configuration file's content,
so a configured alias appears in its output.

The configuration file is read from
**`$XDG_CONFIG_HOME/icechunk/cli-config.yaml`**, falling back to
`$HOME/.config/icechunk/cli-config.yaml`. Its shape is `CliConfig`:

```yaml
repos:
  <alias>: !LocalFileSystem
    path: /absolute/path/to/the/repository
    config: {}          # a RepositoryConfig; every field is optional
```

`repos` maps an alias to a `RepositoryDefinition`, an **externally tagged** enum
with the variants `LocalFileSystem { path, config }`, `S3 { location,
object_store_config, credentials, config }`, `Tigris { … }`, `Azure { location,
object_store_config, credentials, config }` and `GCS { … }`. The file is read
with the same YAML dialect the interface writes it with, which spells an
externally tagged enum as a **`!Variant` tag** on the value — as shown above,
and as `icechunk config list` prints it. The verifier writes only the
`LocalFileSystem` variant, with `config: {}`, relying on every
`RepositoryConfig` field being optional. A configuration file that cannot be
read or parsed is treated as empty, so every alias then resolves to "not found"
and the invocation fails.

`icechunk repo create` on a `LocalFileSystem` entry creates the directory if
needed and leaves `refs/` and `snapshots/` populated under it; the library's
`Repository::exists` then reports `true` for that prefix.

### 12.5 Manifest splitting (milestone 06)

With `ManifestConfig { splitting: Some(ManifestSplittingConfig::with_size(n)), .. }`
an array's chunk references are cut into manifests covering runs of `n`
coordinates per dimension, so a commit writing `k` references along one
dimension produces `ceil(k / n)` manifests, each carrying the references that
fall in its run. `Repository::lookup_snapshot(&id)?.manifests` reports them.
With no splitting configured a commit writes one manifest per array. Every
reference stays readable through the split manifests.

### 12.6 Virtual chunk containers (milestone 07)

```rust
// icechunk::virtual_chunks
pub struct VirtualChunkContainer { pub name: Option<ContainerName>, pub store: ObjectStoreConfig }
impl VirtualChunkContainer {
    pub fn new(url_prefix: String, store: ObjectStoreConfig) -> Result<Self, String>;
    pub fn url_prefix(&self) -> &str;
    pub fn validate_credentials(&self, cred: Option<&Credentials>) -> Result<(), String>;
}
pub struct VirtualChunkResolver { /* … */ }
impl VirtualChunkResolver {
    pub fn new(containers: impl Iterator<Item = VirtualChunkContainer>,
               credentials: HashMap<String, Option<Credentials>>,
               settings: icechunk::storage::Settings) -> Self;
    pub fn matching_container(&self, chunk_location: &str) -> Option<&VirtualChunkContainer>;
}
```

`new` takes no name, so a container it builds has **`name == None`**; the field
survives only for configurations persisted before it was dropped from use, which
is why it keeps `#[serde(default)]`. `url_prefix` is private and read through
`url_prefix()`.

`VirtualChunkContainer::new` **rejects a `url_prefix` that does not end in `/`**,
with an error whose text contains `must end in a / character`. It also requires
the URL scheme to be one the given store serves, and requires a host. The
accepted scheme/store pairs at this release are `s3` with `S3`, `S3Compatible`
or `Tigris`; `gcs` or `gs` with `Gcs`; `az`, `azure` or `abfs` with `Azure`;
`tigris` with `Tigris`; `http`/`https` with `Http`; and `file` with
`LocalFileSystem`. `url_prefix()` returns the prefix verbatim, and
`VirtualChunkContainer` round-trips through serde.

Validation lives in `new`, **not** in `Deserialize`. A persisted
`VirtualChunkContainer` is parsed exactly as it was written — a prefix with no
trailing `/`, or a scheme no store serves, still deserializes — and is repaired
or silently dropped later, never rejected at parse time. A `Deserialize` that
re-ran `new`'s checks would close the only route by which the repair below can
be reached at all.

`VirtualChunkResolver::new` is tolerant of **persisted** configuration: it
appends a missing trailing `/` to each container's prefix and to each
credentials key before revalidating, and it silently drops a container that
still fails validation rather than failing construction.

Because prefixes end in `/`, container matching is **prefix-exact**: a container
for `s3://bucket/data/` matches `s3://bucket/data/x.dat` and does not match
`s3://bucket/database/x.dat`. `Session::matching_container(&VirtualChunkLocation)
-> Option<&VirtualChunkContainer>` exposes the lookup.

---

## 13. Migration from the previous milestone

**Nothing was removed and nothing was renamed** between the previous milestone
and this one. The public `icechunk` surface only grew, changed signature, or
changed behaviour at an unchanged signature. The complete list follows; items
the verifier checks are marked.

### 13.1 Signature changes

| # | item | change | in scope |
| --- | --- | --- | --- |
| M1 | `Session::list_nodes` | gained the `parent_group: &Path` argument | yes |
| M2 | `Repository::reset_branch` | gained `from_snapshot_id: Option<&SnapshotId>` | yes (§8) |
| M3 | `format::snapshot::Snapshot::iter_arc` | gained `parent_group: &Path` | no |
| M4 | `ops::stats::repo_chunks_storage` | returns `ChunkStorageStats`, was `u64` | yes (§6) |
| M5 | `AssetManager::new` | gained trailing `max_concurrent_requests: u16` | yes |
| M6 | `AssetManager::new_no_cache` | gained trailing `max_concurrent_requests: u16` | yes |
| M7 | `AssetManager::new_with_config` | gained trailing `max_concurrent_requests: u16` | no |
| M8 | `storage::s3::get_object_range` | gained trailing `requester_pays: bool` | no |

### 13.2 Struct shape

| # | item | change | in scope |
| --- | --- | --- | --- |
| M9 | `config::S3Options` | +`network_stream_timeout_seconds: Option<u32>`, +`requester_pays: bool`; every struct literal must be updated | yes (§3.5) |
| M10 | `config::RepositoryConfig` | +`max_concurrent_requests: Option<u16>`, between `compression` and `caching` | yes (§3) |
| M11 | `storage::object_store::AzureObjectStoreBackend` | its five fields became `pub` | no |
| M12 | `virtual_chunks::VirtualChunkContainer` | `name` is now always serialized (it kept `#[serde(default)]`, so old payloads still read) | no |
| M13 | `virtual_chunks::S3Fetcher` | gained a private `requester_pays` field; its constructor is unchanged | no |
| M14 | `asset_manager::AssetManager` | gained private concurrency state; its serialized form changed | no |

### 13.3 Enum variants (all additive)

| # | item | change | in scope |
| --- | --- | --- | --- |
| M15 | `store::StoreErrorKind::BadKeyPrefix` | tuple variant became `{ prefix, message }` | no |
| M16 | `repository::RepositoryErrorKind` | +`RepositoryIsV2` | yes (§4) |
| M16b | `repository::RepositoryErrorKind` | +`AcquireError(tokio::sync::AcquireError)` | no — see §14 |
| M17 | `format::manifest::VirtualReferenceErrorKind` | +`AzureConfigurationMustIncludeAccount`, +`Decoding(FromUtf8Error)` | no |

### 13.4 Traits

| # | item | change | in scope |
| --- | --- | --- | --- |
| M18 | `storage::Storage` | +required `has_object`, +provided `has_v2_repo_info`. The trait is sealed, so no downstream implementor breaks | yes (§4.3) |

### 13.5 Behaviour at an unchanged signature

| # | item | change | in scope |
| --- | --- | --- | --- |
| M19 | `RepositoryConfig::merge` | `virtual_chunk_containers` union → **replace** | yes (§3.2) |
| M20 | `CachingConfig::num_snapshot_nodes()` | default 30,000 → **500,000** | yes (§3.4) |
| M21 | `VirtualChunkContainer::validate_credentials` | Azure containers now reject `None` credentials | no |
| M22 | `VirtualChunkContainer::new` | accepts `gs://`, `azure://`, `abfs://`; three error strings reworded | partly (§12.6) |
| M23 | `Store::{list_prefix, list_dir, list_dir_items, getsize_prefix}` | a prefix that is not the root, a group or an array now errors instead of returning nothing | no |
| M24 | `storage::new_tigris_storage` | default endpoint moved to `https://t3.storage.dev`; region header renamed; a consistency header added | no |
| M25 | `storage::{new_s3_storage, new_s3_object_store_storage}` | also reject `t3.storage.dev` endpoints | no |
| M26 | `VirtualChunkResolver::get_fetcher` | the Azure arm is implemented instead of panicking; every URL component is percent-decoded | no |
| M27 | `Session::merge` | `IncompatibleSplittingConfig` now reports the other session's configuration in `theirs` | no |
| M28 | `Repository::{create, open_or_create, save_config}` | storage settings are `backend defaults merged with the user's`, not the user's wholesale | yes (§10) |
| M29 | manifest write failure during flush | returns an error instead of panicking | no |
| M30 | manifest preloading | considers only the first 50 array nodes | no |
| M31 | snapshot and manifest size budgets | raised 100× and 10× respectively; both are private statics, observable only through §3.4 | via M20 |
| M32 | `ConflictDetector` | delete-then-recreate is no longer a new-node conflict | yes (§7) |
| M33 | S3 conditional write | a Ceph `ConcurrentModification` is treated as "did not overwrite" | no |
| M34 | S3 client construction | newest SDK behaviour version, lazy identity cache, 408/429/499 retry classifier, stalled-stream protection | no |
| M35 | GCS credential refresh | refreshes 120–180 s before expiry instead of at expiry | no |

Additive items are described in §§3–11 rather than repeated here:
`icechunk::inspect` (§11), `ChunkStorageStats` (§6),
`DEFAULT_MAX_CONCURRENT_REQUESTS` (§3.3),
`Repository::authorized_virtual_container_prefixes` (§9), `Session::flush` (§8),
`Session::all_node_chunks` (§5) and `Snapshot::get_node_index` (§11). Two more
additions — `format::lookup_index_by_key` and
`virtual_chunks::ObjectStoreFetcher::new_azure` — exist in the release but are
**out of scope**, and §14 says why.

---

## 14. What is out of scope

Named explicitly, so that nothing here is a hidden requirement and nothing here
is mistaken for one.

**Python-only, therefore outside the Rust artifact entirely.**

* `Session.chunk_type` / `Session.chunk_type_async` and the `ChunkType` enum
  (`UNINITIALIZED`, `NATIVE`, `VIRTUAL`, `INLINE`). These live only in the PyO3
  shim crate. The Rust driver is `Session::get_chunk_ref`, and §5 is what is
  in scope.
* `Repository.inspect_snapshot`. There is no such Rust method; the Rust surface
  is the free function `icechunk::inspect::snapshot_json` (§11).
* The asynchronous twin of every I/O method (`*_async`). The Rust crate has been
  fully asynchronous since long before this milestone; the twins are Python
  bindings.
* `ManifestFileInfo` property accessors, and `Session.config` as a Python
  property.
* The `repo.transaction(...)` context manager.

**Infrastructure-only, therefore unreachable from an offline verifier.**

* **Retry behaviour.** Retrying on HTTP 408, 429 and 499, on connection resets,
  and on stalled streams requires a fault-injecting proxy in front of an object
  store (upstream runs one in its compose file for its own flaky-connection
  suite). Neither the retry classifier, the post-connection streaming retry, nor
  the stalled-stream detector is in scope. **Only the configuration surface in
  §10 is** — constructed, merged and read back.
* **HTTP proxy support.** It is inherited from the AWS SDK honouring
  `HTTPS_PROXY` / `HTTP_PROXY` / `ALL_PROXY` / `NO_PROXY`; there is no `proxy`
  identifier anywhere in the crate and nothing to drive offline.
* **S3 requester-pays request behaviour.** The `S3Options::requester_pays`
  *field* is in scope as part of the struct's shape (§3.5); whether the request
  carries the request-payer header is not observable without a bucket.
* Anything requiring MinIO, Azurite, a real cloud account or a public bucket:
  the Azure virtual-chunk fetcher, GCS and Tigris backends, virtual chunk
  *reading* of any kind, and `Storage` trait conformance over a network
  backend. **`virtual_chunks::ObjectStoreFetcher::new_azure`** is the
  constructor of that Azure fetcher, so it is out of scope for the same reason:
  it must exist for the crate to build, and nothing offline can exercise it.
* **`format::lookup_index_by_key`.** It is generic over
  `flatbuffers::Vector<'_, T>`, so calling it at all means linking
  `flatbuffers` into the caller. The verifier's driver crate depends on exactly
  the six crates §1 lists, `flatbuffers` not among them, so this function is
  unreachable from it and is out of scope. Its one public consequence,
  `Snapshot::get_node_index`, **is** in scope (§11).

**Present in the code but not a requirement this milestone checks.**

* **`RepositoryErrorKind::AcquireError`.** It wraps a `tokio::sync::AcquireError`
  from the internal permit semaphore §3.3's `max_concurrent_requests` feeds. That
  error is produced only when the semaphore is closed, which no public entry
  point does, so the variant must exist for the crate to compile and nothing
  offline can raise it. Only `RepositoryIsV2` out of §13.3's additions is in scope.

* `AzureCredentials` has **no** `Anonymous` variant at this release, and the
  Azure virtual-chunk fetcher carries an explicit "support anonymous" to-do.
  Anonymous credentials are in scope for S3 and GCS only (§9).
* The Azure *native* storage backend is unchanged from the previous milestone;
  it is not a new capability here and is out of scope. What is new is Azure as a
  *virtual chunk* location, which needs an emulator.
* The raised flatbuffers table budgets are private statics. Only their public
  consequence, the `num_snapshot_nodes` default (§3.4), is in scope.
* The `NUM_CHUNKS_LIMIT` soft warning on very large change sets: it logs and
  does not error, and log output is out of scope.

**Never in scope anywhere in this task.** Internal module layout, private item
names, the wording of any error message (error **kinds** and **variants** are
in scope; rendered text is not, with the single exception of the
`must end in a / character` substring in §12.6, which the previous milestone
made normative), source formatting, allocation strategy, timing, log output, and
the candidate's own test targets.
