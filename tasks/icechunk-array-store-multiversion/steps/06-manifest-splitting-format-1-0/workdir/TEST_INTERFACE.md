# TEST_INTERFACE.md — milestone 06, manifest splitting and format 1.0

The public contract the verifier for this milestone drives. It is
self-contained: everything the verifier needs — module path, symbol, signature,
argument order, default, error variant, file layout, exit status, build command
— is either here or in a document named in `/app/icechunk-spec/README.md`.

Every signature below was read off the release this milestone corresponds to.
Where this document and a copied specification document disagree,
`/app/icechunk-spec/PROFILE.md` says which wins and why.

§7 lists what is **out of scope**. Read it: several things the documentation
describes at length are deliberately out of scope.

---

## 1. Identity and build

| | |
| --- | --- |
| Workspace root | `/app` |
| Default workspace member | `/app/icechunk` |
| Crate name | `icechunk` |
| Crate version | **`0.3.0`** (the upstream tag for this release, `v1.0.0`, is the *Python* package's version — see PROFILE.md P6-8) |
| Edition | `2024` |
| Workspace members | `icechunk`, `icechunk-python`, **`icechunk-macros`** (new at this milestone) |
| `default-members` | `["icechunk"]` — a bare `cargo build` at `/app` builds only `icechunk` |
| Lockfile | committed at `/app/Cargo.lock` |

The verifier runs, in this order:

```
cargo build --manifest-path /app/Cargo.toml
cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk
```

Both must succeed. The second produces the `icechunk` binary, which the
verifier reaches through the `ICECHUNK_TEST_CLI_BIN` environment variable; see
§6.5.

Cargo features that must exist, neither of them default:

* `cli` — enables `icechunk::cli::{config, interface}` and the
  `[[bin]] name = "icechunk"` target.
* `logs` — enables `icechunk::initialize_tracing`.

**`icechunk-macros` is a new workspace member**, a `proc-macro` crate exporting
two attribute macros (`tokio_test`, `test`). It is a **dev-dependency of
`icechunk` only** and is **not re-exported** from `icechunk`. Nothing outside
the workspace can reach it, and nothing in this milestone is in scope for it; it
exists so that `/app/icechunk/tests/*.rs` and the crate's own unit tests can use
it. The verifier's drivers use plain `#[tokio::test]`.

### 1.1 The crate root

`icechunk/src/lib.rs` declares at least these public modules — unchanged from
the previous milestone; a further public module of your own is neither required
nor forbidden:

```rust
pub mod asset_manager;  pub mod change_set;  pub mod cli;      pub mod config;
pub mod conflicts;      pub mod error;       pub mod format;   pub mod ops;
pub mod refs;           pub mod repository;  pub mod session;  pub mod storage;
pub mod store;          pub mod virtual_chunks;
```

and these crate-root re-exports:

```rust
pub use config::{ObjectStoreConfig, RepositoryConfig};
pub use repository::Repository;
pub use storage::{
    ObjectStorage, Storage, StorageError, new_in_memory_storage,
    new_local_filesystem_storage, new_s3_storage,
};
pub use store::Store;
```

One module is added one level down: **`icechunk::ops::manifests`** (§3.9).
`icechunk::strategies` remains `#[cfg(test)]` and is invisible from outside.

### 1.2 Storage constructors the verifier uses

```rust
pub async fn new_in_memory_storage() -> StorageResult<Arc<dyn Storage>>;
pub async fn new_local_filesystem_storage(path: &std::path::Path)
    -> StorageResult<Arc<dyn Storage>>;
```

Both results must coerce to `Arc<dyn Storage + Send + Sync>`. The verifier uses
only these two; it never opens a cloud backend.

---

## 2. Repository construction — the breaking change

All four constructors take **`authorize_virtual_chunk_access`** as their third
argument, keyed by **url prefix**:

```rust
impl Repository {
    pub async fn create(
        config: Option<RepositoryConfig>,
        storage: Arc<dyn Storage + Send + Sync>,
        authorize_virtual_chunk_access: HashMap<String, Option<Credentials>>,
    ) -> RepositoryResult<Self>;

    pub async fn open(/* identical */) -> RepositoryResult<Self>;
    pub async fn open_or_create(/* identical */) -> RepositoryResult<Self>;

    pub fn reopen(
        &self,
        config: Option<RepositoryConfig>,
        authorize_virtual_chunk_access: Option<HashMap<String, Option<Credentials>>>,
    ) -> RepositoryResult<Self>;
}
```

`Credentials` is `icechunk::config::Credentials`. `reopen` merges the given
config over the repository's current one and, with `None`, keeps the current
authorisation map; it does not touch storage.

Other `Repository` members the verifier uses, all unchanged from the previous
milestone:

```rust
pub async fn exists(storage: &(dyn Storage + Send + Sync)) -> RepositoryResult<bool>;
pub fn config(&self) -> &RepositoryConfig;
pub fn storage_settings(&self) -> &storage::Settings;
pub fn storage(&self) -> &Arc<dyn Storage + Send + Sync>;
pub fn asset_manager(&self) -> &Arc<AssetManager>;
pub async fn save_config(&self) -> RepositoryResult<storage::VersionInfo>;
pub async fn writable_session(&self, branch: &str) -> RepositoryResult<Session>;
pub async fn readonly_session(&self, version: &VersionInfo) -> RepositoryResult<Session>;
pub async fn lookup_snapshot(&self, id: &SnapshotId) -> RepositoryResult<SnapshotInfo>;
pub async fn lookup_branch(&self, branch: &str) -> RepositoryResult<SnapshotId>;
pub async fn lookup_tag(&self, tag: &str) -> RepositoryResult<SnapshotId>;
pub async fn list_branches(&self) -> RepositoryResult<BTreeSet<String>>;
pub async fn list_tags(&self) -> RepositoryResult<BTreeSet<String>>;
pub async fn create_branch(&self, branch: &str, id: &SnapshotId) -> RepositoryResult<()>;
pub async fn create_tag(&self, tag: &str, id: &SnapshotId) -> RepositoryResult<()>;
pub async fn delete_tag(&self, tag: &str) -> RepositoryResult<()>;
pub async fn reset_branch(&self, branch: &str, id: &SnapshotId) -> RepositoryResult<()>;
pub async fn ancestry<'a>(&'a self, version: &VersionInfo)
    -> RepositoryResult<impl Stream<Item = RepositoryResult<SnapshotInfo>> + 'a>;
```

`icechunk::repository::VersionInfo` is
`SnapshotId(SnapshotId) | TagRef(String) | BranchTipRef(String) | AsOf { branch, at }`.

---

## 3. Manifest splitting

The capability this milestone adds. `/app/icechunk-spec/guide/performance.md`
§§"Splitting manifests", "Configuring splitting", "Splitting behaviour",
"Rewriting manifests" is the normative prose; PROFILE.md P6-1 fixes the four
points the prose leaves open, and P6-2 the two constructors that do not exist
in Rust.

### 3.1 Which arrays a rule selects — `icechunk::config`

```rust
#[derive(Debug, PartialEq, Eq, Serialize, Hash, Deserialize, Clone)]
#[serde(rename_all = "snake_case")]
pub enum ManifestSplitCondition {
    Or(Vec<ManifestSplitCondition>),
    And(Vec<ManifestSplitCondition>),
    PathMatches { regex: String },
    NameMatches { regex: String },
    AnyArray,
}

impl ManifestSplitCondition {
    pub fn matches(&self, path: &icechunk::format::Path) -> bool;
}
```

Semantics:

* `AnyArray` — true for every array, including the root path.
* `PathMatches { regex }` — an **unanchored** regex search over the array's
  full path rendered as a string (`/ocean/temperature`).
* `NameMatches { regex }` — the same search over the array's *name*, i.e. the
  last path segment. The root path has no name, so `NameMatches` is false for
  it whatever the regex.
* `Or(v)` — true if any clause is true. `Or(vec![])` is **false**.
* `And(v)` — true if every clause is true. `And(vec![])` is **true**.
* **A regex that fails to compile makes that clause `false`, silently.** There
  is no error path. A rule whose condition cannot compile never fires, and the
  array keeps a single manifest.

There are **no** `and_conditions` / `or_conditions` constructors and no `&` /
`|` operators on this type in Rust; a caller builds the variant. See
PROFILE.md P6-2.

The type must round-trip through `serde_json` unchanged.

### 3.2 How each axis is split — `icechunk::config`

```rust
#[derive(Debug, Hash, PartialEq, Eq, Serialize, Deserialize, Clone)]
pub enum ManifestSplitDimCondition {
    Axis(usize),
    DimensionName(String),   // the String is a REGEX over the dimension's name
    Any,
}

#[derive(Debug, PartialEq, Eq, Serialize, Deserialize, Clone)]
pub struct ManifestSplitDim {
    pub condition: ManifestSplitDimCondition,
    pub num_chunks: u32,
}

#[derive(Debug, PartialEq, Eq, Serialize, Deserialize, Clone)]
pub struct ManifestSplittingConfig {
    pub split_sizes: Option<Vec<(ManifestSplitCondition, Vec<ManifestSplitDim>)>>,
}
```

* `Axis(n)` matches the axis whose index is `n`.
* `DimensionName(regex)` compiles `regex` and searches the dimension's name. An
  axis whose dimension name is unspecified never matches, and neither does any
  axis when the array has no dimension names at all.
* `Any` matches any **remaining** axis — one that no earlier spec has claimed.

`split_sizes` is an ordered `Vec` of rules, and **the first match wins per
axis**, both across rules and within one rule's `Vec<ManifestSplitDim>`. Once
an axis is claimed it is never revisited.

### 3.3 Edges — `icechunk::format::manifest`

```rust
pub fn uniform_manifest_split_edges(num_chunks: u32, split_size: &u32) -> Vec<u32>;
```

The edge list is `0, split_size, 2*split_size, …` up to and including
`num_chunks`, with `num_chunks` appended when `split_size` does not divide it.
So:

| `num_chunks` | `split_size` | edges |
| --- | --- | --- |
| 25 | 12 | `[0, 12, 24, 25]` |
| 10 | 9 | `[0, 9, 10]` |
| 10 | 3 | `[0, 3, 6, 9, 10]` |
| 12 | 3 | `[0, 3, 6, 9, 12]` |
| 10 | 5 | `[0, 5, 10]` |
| 10 | 10 | `[0, 10]` |
| 1 | 1 | `[0, 1]` |
| 4 | 9 | `[0, 4]` |
| 4 | `u32::MAX` | `[0, 4]` |
| 4 | 1 | `[0, 1, 2, 3, 4]` |

### 3.4 The grid — `icechunk::format::manifest`

```rust
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ManifestSplits(pub Vec<ManifestExtents>);

impl ManifestSplits {
    pub fn from_extents(extents: Vec<ManifestExtents>) -> Self;  // panics if empty
    pub fn from_edges(iter: impl IntoIterator<Item = Vec<u32>>) -> Self;
    pub fn iter(&self) -> impl Iterator<Item = &ManifestExtents>;
    pub fn len(&self) -> usize;
    pub fn is_empty(&self) -> bool;
    pub fn compatible_with(&self, other: &Self) -> bool;
    pub fn find<'a>(&'a self, coord: &'a ChunkIndices) -> Option<&'a ManifestExtents>;
    pub fn position(&self, coord: &ChunkIndices) -> Option<usize>;
}
```

`from_edges` takes one edge list per axis, forms the consecutive pairs of each,
and takes the **multi-cartesian product**, producing one `ManifestExtents` per
cell in **row-major order — the last axis varies fastest**:

```rust
ManifestSplits::from_edges(vec![vec![0u32, 1, 2], vec![3u32, 4, 5]])
== ManifestSplits::from_extents(vec![
       ManifestExtents::new(&[0, 3], &[1, 4]),
       ManifestExtents::new(&[0, 4], &[1, 5]),
       ManifestExtents::new(&[1, 3], &[2, 4]),
       ManifestExtents::new(&[1, 4], &[2, 5]),
   ])
```

`position` returns the index of the **first** cell containing the coordinate
and `find` returns that cell; both return `None` when no cell contains it
(out of bounds, or a cell that holds no data). Both **require** the coordinate
to have the grid's dimensionality: that is a caller precondition, not a checked
rejection — what a grid of one dimensionality does with a coordinate of another
is undefined here and is out of scope (§7).

`compatible_with` is true when no cell of either grid **partially** overlaps a
cell of the other. Identical grids are compatible; a grid whose cells straddle
the other's boundaries is not, and neither is a coarser grid whose single cell
spans several of the other's.

```rust
pub fn find_coord<'a, I>(iter: I, coord: &'a ChunkIndices)
    -> Option<(usize, &'a ManifestExtents)>
where I: Iterator<Item = &'a ManifestExtents>;
```
is the free function in `icechunk::session` that both of them are built on; it
yields the index and the cell.

### 3.5 Bounding boxes — `icechunk::format::manifest`

```rust
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Overlap { Complete, Partial, None }

#[derive(Debug, Clone, Hash, PartialEq, Eq, Serialize, Deserialize)]
pub struct ManifestExtents(Vec<Range<u32>>);

impl ManifestExtents {
    pub const ALL: Self;                                                  // the universal set
    pub fn new(from: &[u32], to: &[u32]) -> Self;
    pub fn from_ranges_iter(ranges: impl IntoIterator<Item = Range<u32>>) -> Self;
    pub fn contains(&self, coord: &[u32]) -> bool;
    pub fn iter(&self) -> impl Iterator<Item = &Range<u32>>;
    pub fn len(&self) -> usize;
    pub fn is_empty(&self) -> bool;
    pub fn intersection(&self, other: &Self) -> Option<Self>;
    pub fn union(&self, other: &Self) -> Self;
    pub fn overlap_with(&self, other: &Self) -> Overlap;
    pub fn matches(&self, other: &ManifestExtents) -> bool;
}
```

* Every range is half-open: `new(&[0], &[2])` contains `0` and `1`, not `2`.
* `intersection` is `None` when any axis would be empty; `union` is the
  bounding box of both.
* **`overlap_with` is deliberately not symmetric**: `a.overlap_with(b)` asks
  whether `b` fully covers `a`. So a small box against a box that contains it
  is `Complete`, and the reverse is `Partial`. Disjoint boxes are `None` in
  both directions.
* `matches` is equality for two ordinary boxes: `x.matches(&y)` is true only
  when `x == y`. `ALL` is the exception, below.
* `ALL` is the empty-vector sentinel standing for "every coordinate".
  `ALL.matches(x)` is true for any `x`; `x.overlap_with(&ALL)` is `Complete`;
  `ALL.overlap_with(&x)` is `Partial`; `ALL.intersection(&x)` is `Some(x)` and
  `ALL.union(&x)` is `ALL`. `ALL.len()` is `0` and `ALL.is_empty()` is true.
  Calling `intersection` or `union` **on** a non-`ALL` box **with** `ALL` is
  not defined and is out of scope.

### 3.6 The configuration objects — `icechunk::config`

```rust
impl Default for ManifestSplittingConfig { /* … */ }   // never splits
impl ManifestSplittingConfig {
    pub fn with_size(split_size: u32) -> Self;
}
```

`ManifestSplittingConfig::with_size(n)` is exactly

```rust
ManifestSplittingConfig {
    split_sizes: Some(vec![(
        ManifestSplitCondition::PathMatches { regex: r".*".to_string() },
        vec![ManifestSplitDim {
            condition: ManifestSplitDimCondition::Any,
            num_chunks: n,
        }],
    )]),
}
```

and `ManifestSplittingConfig::default()` is the same shape with
`num_chunks: u32::MAX`, i.e. one manifest per array. `split_sizes: None`
behaves as the default.

```rust
#[derive(Debug, PartialEq, Eq, Serialize, Deserialize, Clone, Default)]
pub struct ManifestConfig {
    pub preload: Option<ManifestPreloadConfig>,
    pub splitting: Option<ManifestSplittingConfig>,   // NEW at this milestone
}

impl ManifestConfig {
    pub fn merge(&self, other: Self) -> Self;              // right-biased, per field
    pub fn preload(&self) -> &ManifestPreloadConfig;
    pub fn splitting(&self) -> &ManifestSplittingConfig;   // NEW
    pub fn empty() -> Self;                                // NEW
}
```

`ManifestConfig::empty()` returns `preload: Some(ManifestPreloadConfig {
max_total_refs: None, preload_if: None })` and `splitting: None`.

`RepositoryConfig` gains nothing new, but its `manifest` field now carries
`splitting`:

```rust
pub struct RepositoryConfig {
    pub inline_chunk_threshold_bytes: Option<u16>,      // default 512
    pub get_partial_values_concurrency: Option<u16>,
    pub compression: Option<CompressionConfig>,
    pub caching: Option<CachingConfig>,
    pub storage: Option<storage::Settings>,
    pub virtual_chunk_containers: Option<HashMap<String, VirtualChunkContainer>>,
    pub manifest: Option<ManifestConfig>,
}
impl RepositoryConfig { pub fn manifest(&self) -> &ManifestConfig; /* … */ }
```

The whole configuration must round-trip through `serde_json` unchanged, because
it is what `save_config` persists.

### 3.7 Deriving a grid, and what a commit writes

```rust
// icechunk::config::ManifestSplittingConfig, implemented in icechunk::session
impl ManifestSplittingConfig {
    pub fn get_split_sizes(
        &self,
        path: &Path,
        shape: &ArrayShape,
        dimension_names: &Option<Vec<DimensionName>>,
    ) -> ManifestSplits;
}
```

Algorithm, fixed:

1. Start from one cell per axis: `edges[axis] = [0, num_chunks[axis]]`, where
   `num_chunks` comes from `ArrayShape::num_chunks()`.
2. If `split_sizes` is `None`, substitute the default's.
3. Walk the rules **in order**. For each rule whose condition matches `path`,
   walk the axes; skip an axis already claimed; otherwise take the **first**
   `ManifestSplitDim` whose condition matches that axis, set
   `edges[axis] = uniform_manifest_split_edges(num_chunks[axis], &num_chunks_of_that_spec)`,
   mark the axis claimed and move to the next axis.
4. Return `ManifestSplits::from_edges(edges)`.

At commit time the session writes **at most one manifest per non-empty cell**.
An empty cell writes nothing. So, for an array of 10 chunks along one axis with
split size 3:

* the backend holds **4** manifest objects
  (`Storage::list_manifests(&settings)` counts them);
* `Repository::lookup_snapshot(&id).manifests` has **4** entries, with distinct
  ids, `num_chunk_refs` summing to 10 and `size_bytes > 0` each;
* the array node's `NodeData::Array { manifests, .. }` holds 4 `ManifestRef`s,
  each with a **populated, non-empty** `extents`.

With no splitting configured the same array produces exactly **one** manifest.

A commit rewrites only what it must. For a cell of the **active** grid that
this session wrote nothing into, an existing manifest whose bounding box lies
wholly inside that cell is carried into the new snapshot **unchanged, id and
all**; a cell this session did write, and an existing manifest that straddles a
cell boundary, produce a new manifest object. So an array whose first five
chunks were committed under `with_size(1)` holds five manifests; re-opened
under `with_size(5)` — whose cells `[0,5)` and `[5,10)` each wholly contain
those bounding boxes, so the two grids are compatible — and appended to in the
second cell, the next commit adds exactly **one** manifest and keeps the five:
six manifest objects in the backend and six entries in the new snapshot, with
`num_chunk_refs` still summing to the number of references written.
`guide/performance.md` §"Splitting behaviour" states this for the same example
("only one new manifest with the 3 new chunk refs has been written"); §3.9's
`rewrite_manifests` is how a caller forces a whole array to be re-laid-out
instead.

```rust
// icechunk::format::snapshot
pub struct SnapshotInfo {
    pub id: SnapshotId,
    pub parent_id: Option<SnapshotId>,
    pub flushed_at: DateTime<Utc>,
    pub message: String,
    pub metadata: SnapshotProperties,        // BTreeMap<String, serde_json::Value>
    pub manifests: Vec<ManifestFileInfo>,
}
impl SnapshotInfo { pub fn is_initial(&self) -> bool; }

pub struct ManifestFileInfo {
    pub id: ManifestId,
    pub size_bytes: u64,
    pub num_chunk_refs: u32,
}

pub struct ArrayShape(/* private */);
impl ArrayShape {
    pub fn new<I: IntoIterator<Item = (u64, u64)>>(it: I) -> Option<Self>;  // (length, chunk length)
    pub fn num_chunks(&self) -> impl Iterator<Item = u32>;
    pub fn len(&self) -> usize;
}

pub enum DimensionName { NotSpecified, Name(String) }  // `impl From<&str>`: "" is NotSpecified
```

### 3.8 The splitting policy never changes what a reader sees

This is a property, and it is checked as one. For the same committed data:

* reading every chunk through a repository configured with **no** splitting,
  with `with_size(1)`, with `with_size(7)` or with the default policy must
  return byte-identical payloads;
* re-laying-out the manifests (§3.9) at split size 12 and then at 4 must leave
  every chunk's bytes unchanged, while the manifest count in the new snapshot
  becomes 1 and then 3 for a 10-chunk array.

The splitting policy is a storage-layout decision only.

### 3.9 Rewriting manifests

```rust
// icechunk::ops::manifests  (NEW module)
#[derive(Debug, thiserror::Error)]
pub enum ManifestOpsError {
    #[error("error rewriting manifests")]
    ManifestRewriteError(#[from] Box<SessionError>),
}
pub type ManifestOpsResult<A> = Result<A, ManifestOpsError>;

pub async fn rewrite_manifests(
    repository: &Repository,
    branch: &str,
    message: &str,
    properties: Option<SnapshotProperties>,
) -> ManifestOpsResult<SnapshotId>;

// icechunk::session::Session
pub async fn rewrite_manifests(&mut self, message: &str,
                               properties: Option<SnapshotProperties>)
    -> SessionResult<SnapshotId>;
```

Both re-write every chunk reference under the repository's **currently active**
splitting policy and create one new commit on the branch, whose message is the
one given. Both work on a repository with no pending changes — the
"nothing to commit" guard does not apply.

Both additionally stamp the active policy into the new snapshot's metadata
under the key **`splitting_config`**, whose value is
`serde_json::to_value(config.manifest().splitting())`. Any `properties` the
caller passes are kept alongside it. An ordinary `Session::commit` does **not**
write that key.

### 3.10 The grid a session caches per array

```rust
impl Session {
    pub fn lookup_splits(&self, node_id: &NodeId) -> Option<&ManifestSplits>;
    pub async fn add_array(&mut self, path: Path, shape: ArrayShape,
                           dimension_names: Option<Vec<DimensionName>>,
                           user_data: Bytes) -> SessionResult<()>;
    pub async fn update_array(&mut self, path: &Path, shape: ArrayShape,
                              dimension_names: Option<Vec<DimensionName>>,
                              user_data: Bytes) -> SessionResult<()>;
    pub async fn set_chunk_ref(&mut self, path: Path, coord: ChunkIndices,
                               data: Option<ChunkPayload>) -> SessionResult<()>;
}
```

* `add_array` derives and caches the array's grid, so `lookup_splits` answers
  immediately afterwards, before anything is written or committed.
* `update_array` re-derives it, so a **dimension rename or a resize changes the
  grid inside the open session**, and a rule that previously did not match may
  now claim an axis. Data already written in that session stays readable at
  every coordinate across the regrid.
* `set_chunk_ref` at a coordinate outside the array's shape is
  `SessionErrorKind::InvalidIndex { coords, path }` — a named error, never a
  panic.

New `SessionErrorKind` variants at this milestone:
`JsonSerializationError`, `InvalidIndexForSplitManifests { coords }`,
`IncompatibleSplittingConfig { ours, theirs }`, `ManifestCreationError`.
`SessionError` is `icechunk::error::ICError<SessionErrorKind>`, so a test
matches on its public `kind` field.

### 3.11 Manifest preloading

Unchanged from the previous milestone except that `ManifestConfig` now has two
fields. In scope are the predicate and the accessors only — see PROFILE.md P6-4
and §7.

```rust
pub enum ManifestPreloadCondition {
    Or(Vec<ManifestPreloadCondition>),
    And(Vec<ManifestPreloadCondition>),
    PathMatches { regex: String },
    NameMatches { regex: String },
    NumRefs { from: std::ops::Bound<u32>, to: std::ops::Bound<u32> },
    True,
    False,
}
impl ManifestPreloadCondition {
    // implemented in icechunk::repository
    pub fn matches(&self, path: &Path, info: &ManifestFileInfo) -> bool;
}

#[derive(Default)]
pub struct ManifestPreloadConfig {
    pub max_total_refs: Option<u32>,
    pub preload_if: Option<ManifestPreloadCondition>,
}
impl ManifestPreloadConfig {
    pub fn merge(&self, other: Self) -> Self;
    pub fn max_total_refs(&self) -> u32;                    // default 10_000
    pub fn preload_if(&self) -> &ManifestPreloadCondition;  // the default below
}
```

`True` and `False` are the constant predicates. `Or(v)` is true if any clause
is, `And(v)` if every clause is, and `PathMatches` / `NameMatches` behave
exactly as their split-condition namesakes in §3.1 — unanchored regex searches
over the array's path and its name. `NumRefs { from, to }` tests
`info.num_chunk_refs` against the two bounds with ordinary `RangeBounds`
semantics, so `Included`, `Excluded` and `Unbounded` all work. The type must
round-trip through `serde_json` unchanged.

The default `preload_if` selects **coordinate-axis array names only**, and caps
them at **1000 chunk references**. Concretely, for a manifest of one reference
it is true for `/time`, `/latitude`, `/longitude`, `/x`, `/y` and `/depth`, and
false for `/array`, `/temperature` and `/nottime`; for `/time` it is true at
1000 references and false at 1001. The 1000 cap appears in no published
document — see PROFILE.md P6-4.

---

## 4. The rest of the new surface

### 4.1 The fixed first-snapshot id

```rust
// icechunk::format::snapshot::Snapshot
pub const INITIAL_COMMIT_MESSAGE: &'static str = "Repository initialized";
pub const INITIAL_SNAPSHOT_ID: SnapshotId = SnapshotId::new([
    0x0b, 0x1c, 0xc8, 0xd6, 0x78, 0x75, 0x80, 0xf0, 0xe3, 0x3a, 0x65, 0x34,
]);
```

Those twelve bytes render, through the project's Crockford base32 alphabet, as
the string **`1CECHNKREP0F1RSTCMT0`** — so the first snapshot of *every*
repository is the object `snapshots/1CECHNKREP0F1RSTCMT0`. That is the point of
the constant: **one `lookup_snapshot` at that id identifies a prefix as an
Icechunk repository.**

Required behaviour:

* `String::from(&Snapshot::INITIAL_SNAPSHOT_ID) == "1CECHNKREP0F1RSTCMT0"`, and
  `SnapshotId::try_from("1CECHNKREP0F1RSTCMT0")` round-trips back to it.
* `repo.lookup_snapshot(&Snapshot::INITIAL_SNAPSHOT_ID)` succeeds on any
  repository and returns a `SnapshotInfo` with that id, the message
  `"Repository initialized"`, no parent, `is_initial() == true` and no
  manifests.
* It is the last entry of every ancestry.
* It is **the same id in every repository** — the first snapshot's id is no
  longer random. Any earlier assertion that two fresh repositories have
  different root ids is now false.

`SnapshotId` is `ObjectId<12, _>`: `SnapshotId::new([u8; 12])` is a `const fn`,
the byte array is the public field `.0`, `SnapshotId::FAKE` is all zeroes,
`SnapshotId::random()` is a fresh id, and `String::from(&id)` /
`id.to_string()` / `SnapshotId::try_from(&str)` are the Crockford conversions.

### 4.2 Commit with rebase

```rust
impl Session {
    pub async fn commit(&mut self, message: &str,
                        properties: Option<SnapshotProperties>)
        -> SessionResult<SnapshotId>;

    pub async fn commit_rebasing<F1, F2, Fut1, Fut2>(
        &mut self,
        solver: &dyn ConflictSolver,
        rebase_attempts: u16,
        message: &str,
        properties: Option<SnapshotProperties>,
        before_rebase: F1,
        after_rebase: F2,
    ) -> SessionResult<SnapshotId>
    where F1: Fn(u16) -> Fut1, F2: Fn(u16) -> Fut2,
          Fut1: Future<Output = ()>, Fut2: Future<Output = ()>;

    pub async fn rebase(&mut self, solver: &dyn ConflictSolver) -> SessionResult<()>;
    pub async fn merge(&mut self, other: Session) -> SessionResult<()>;
    pub fn as_bytes(&self) -> SessionResult<Vec<u8>>;
    pub fn from_bytes(bytes: Vec<u8>) -> SessionResult<Self>;
    pub fn branch(&self) -> Option<&str>;
    pub fn read_only(&self) -> bool;
    pub fn has_uncommitted_changes(&self) -> bool;
}
```

`commit_rebasing` commits; on `SessionErrorKind::Conflict` it calls
`before_rebase(attempt)`, rebases with `solver`, calls `after_rebase(attempt)`
and commits again, where `attempt` counts from 1. After `rebase_attempts`
rounds it makes one final commit attempt and returns whatever that produces.
A caller that wants no callbacks passes `|_| async {}` for each.

So, against a writer that re-conflicts after every rebase, `commit_rebasing`
with `rebase_attempts = 3` invokes each callback exactly **3** times and ends in
`Err(SessionError { kind: SessionErrorKind::Conflict { .. }, .. })`. Against a
concurrent commit that touches disjoint chunks, `BasicConflictSolver::default()`
lets it succeed and both writes survive.

There is **no `rebase_with` identifier** in the Rust crate and no
`Repository::commit`; see PROFILE.md P6-3.

`Session::merge` now takes a whole `Session` (it used to take a `ChangeSet`),
which is what makes the "serialise, ship, reduce, commit once" distributed
pattern work: `worker_a.merge(Session::from_bytes(worker_b.as_bytes()?)?)`.
Merging two sessions whose grids disagree is
`SessionErrorKind::IncompatibleSplittingConfig`.

`ConflictSolver` implementations:
`icechunk::conflicts::detector::ConflictDetector` (detects, never solves) and
`icechunk::conflicts::basic_solver::BasicConflictSolver` (`Default` available).

Both read chunk writes at **chunk granularity**, and that is what makes
`rebase` usable at all: two sides that wrote the *same* chunk coordinate of the
same array are in conflict, and two sides that wrote *different* coordinates of
the same array are not. So `Session::rebase(&ConflictDetector)` **succeeds** on
a concurrent commit that touched only other coordinates — the detector's
"never solves" is about conflicts it has found, not about every concurrent
write. Metadata is coarser: a concurrent update or deletion of the same node
conflicts whatever the chunks did.

### 4.3 Retry settings

The Rust name is **`RetriesSettings`**, in `icechunk::storage`. (`StorageSettings`
and `StorageRetriesSettings` are the *Python* class names and do not exist in
Rust.)

```rust
#[derive(Debug, PartialEq, Eq, Serialize, Deserialize, Clone, Default)]
pub struct RetriesSettings {
    pub max_tries: Option<std::num::NonZeroU16>,
    pub initial_backoff_ms: Option<u32>,
    pub max_backoff_ms: Option<u32>,
}
impl RetriesSettings {
    pub fn max_tries(&self) -> NonZeroU16;    // default 10
    pub fn initial_backoff_ms(&self) -> u32;  // default 100
    pub fn max_backoff_ms(&self) -> u32;      // default 180_000 (3 * 60 * 1000)
    pub fn merge(&self, other: Self) -> Self; // right-biased, per field
}

pub struct Settings {                          // icechunk::storage::Settings
    pub concurrency: Option<ConcurrencySettings>,
    pub retries: Option<RetriesSettings>,                        // NEW
    pub unsafe_use_conditional_update: Option<bool>,
    pub unsafe_use_conditional_create: Option<bool>,
    pub unsafe_use_metadata: Option<bool>,
    pub storage_class: Option<String>,                           // NEW
    pub metadata_storage_class: Option<String>,                  // NEW
    pub chunks_storage_class: Option<String>,                    // NEW
    pub minimum_size_for_multipart_upload: Option<u64>,          // NEW
}
impl Settings {
    pub fn retries(&self) -> &RetriesSettings;                   // NEW
    pub fn metadata_storage_class(&self) -> Option<&String>;     // falls back to storage_class
    pub fn chunks_storage_class(&self) -> Option<&String>;       // falls back to storage_class
    pub fn minimum_size_for_multipart_upload(&self) -> u64;      // default 100 * 1024 * 1024
}
```

Defaults live in the accessors, not in the fields: `RetriesSettings::default()`
has all three fields `None` and still answers 10 / 100 / 180000.

The **in-memory backend disables retries** so tests never sleep:
`storage.default_settings().retries()` answers `max_tries() == 1` and both
backoffs `0`. A `RepositoryConfig { storage: Some(settings), .. }` overrides
that, and `Repository::storage_settings()` reads the override back.

### 4.4 Virtual chunk containers and their authorisation

```rust
// icechunk::virtual_chunks
pub struct VirtualChunkContainer {
    pub name: Option<ContainerName>,   // kept only for deserialising old configs
    /* url_prefix is PRIVATE */
    pub store: ObjectStoreConfig,
}
impl VirtualChunkContainer {
    pub fn new(url_prefix: String, store: ObjectStoreConfig) -> Result<Self, String>;
    pub fn url_prefix(&self) -> &str;
    pub fn validate_credentials(&self, cred: Option<&Credentials>)
        -> Result<(), VirtualChunkCredentialsError>;
}

pub struct VirtualChunkResolver { /* … */ }
impl VirtualChunkResolver {
    pub fn new(
        containers: impl Iterator<Item = VirtualChunkContainer>,
        credentials: HashMap<String, Option<Credentials>>,
        settings: storage::Settings,
    ) -> Self;
    pub async fn fetch_chunk(&self, chunk_location: &str,
                             range: &Range<ChunkOffset>,
                             checksum: Option<&Checksum>)
        -> Result<Bytes, VirtualReferenceError>;
}
```

`VirtualChunkContainer::new` is the **only** way to build one — the struct
cannot be written as a literal because `url_prefix` is private. It validates
the prefix against the store type and returns `Err(String)` on a mismatch: an
`s3://` prefix with a local-filesystem store is rejected, and an `http://`
prefix with no host is rejected.

`RepositoryConfig` keys containers by **url prefix**:

```rust
pub fn set_virtual_chunk_container(&mut self, cont: VirtualChunkContainer);
pub fn get_virtual_chunk_container(&self, url_prefix: &str) -> Option<&VirtualChunkContainer>;
pub fn virtual_chunk_containers(&self) -> impl Iterator<Item = &VirtualChunkContainer>;
pub fn clear_virtual_chunk_containers(&mut self);
```

`RepositoryConfig::default().virtual_chunk_containers()` yields **nothing**:
there are no built-in containers at all (PROFILE.md P6-7).

`ObjectStoreConfig` is
`InMemory | LocalFileSystem(PathBuf) | Http(HashMap<String,String>) |
S3Compatible(S3Options) | S3(S3Options) | Gcs(HashMap<String,String>) |
Azure(HashMap<String,String>) | Tigris(S3Options)`. `Http` is new. For a
**virtual chunk container** backed by `LocalFileSystem(path)`, the `path`
payload is not used to locate the chunk: a `file://` virtual reference is an
absolute url and is read at the location it names. The payload must be present
because the variant carries it; any value, `PathBuf::new()` included, behaves
the same for virtual containers.

`icechunk::config::Credentials` is
`S3(S3Credentials) | Gcs(GcsCredentials) | Azure(AzureCredentials)`, and the
three credential enums the verifier names are

```rust
// icechunk::config
pub enum S3Credentials {
    FromEnv, Anonymous, Static(S3StaticCredentials),
    Refreshable(Arc<dyn S3CredentialsFetcher>),
}
pub enum GcsCredentials {
    FromEnv, Anonymous, Static(GcsStaticCredentials),
    Refreshable(Arc<dyn GcsCredentialsFetcher>),
}
pub enum AzureCredentials { FromEnv, Static(AzureStaticCredentials) }

pub struct S3Options {
    pub region: Option<String>,
    pub endpoint_url: Option<String>,
    pub anonymous: bool,
    pub allow_http: bool,
    pub force_path_style: bool,
}
```

The verifier builds only the credential-free variants — `S3Credentials::Anonymous`,
`GcsCredentials::Anonymous`, `AzureCredentials::FromEnv` — and only to hand them
to `validate_credentials` or to a constructor; it never reaches a remote
endpoint, and the `S3Options` it builds is never connected to (§7).

`validate_credentials` checks one credential against the container's store
type, and returns `Err` (the error type is a `String`, whose text is not part
of the contract) when they do not go together:

* **every** store accepts `None` — "authorised, use the natural credential";
* `S3`, `S3Compatible` and `Tigris` accept `Credentials::S3`, `Gcs` accepts
  `Credentials::Gcs`, `Azure` accepts `Credentials::Azure`;
* `InMemory`, `LocalFileSystem` and `Http` accept **no** credential at all;
* every other pairing is an error.

The repository constructors run that check over every entry of
`authorize_virtual_chunk_access` whose url prefix names a declared container,
so `Repository::{create, open, open_or_create}` fails when a container is
authorised with a credential its store cannot take. Which error it fails with
is out of scope.

Resolution outcomes, both raised **before any I/O**:

| situation | error |
| --- | --- |
| a declared container claims the url, but its prefix is absent from the authorisation map | `VirtualReferenceErrorKind::UnauthorizedVirtualChunkContainer(Box<VirtualChunkContainer>)` — new at this milestone |
| no declared container claims the url | `VirtualReferenceErrorKind::NoContainerForUrl(String)` |

`VirtualReferenceError` is `ICError<VirtualReferenceErrorKind>`, matched on its
public `kind` field. Error message wording is not part of the contract.

An authorised `file://` container resolves end to end: with the container
declared in `virtual_chunk_containers` and its prefix mapped to `None` in
`authorize_virtual_chunk_access`, a `ChunkPayload::Virtual(VirtualChunkRef {
location: VirtualChunkLocation::from_absolute_path("file:///…"), offset,
length, checksum })` reads back the bytes at that offset. Opening the same
repository with an empty authorisation map makes the same read fail.

---

## 5. Migration from the previous milestone

Everything a driver written against the previous milestone must change.

1. **`Repository::{create, open, open_or_create}`** — the third argument was
   `virtual_chunk_credentials: HashMap<ContainerName, Credentials>` and is now
   `authorize_virtual_chunk_access: HashMap<String, Option<Credentials>>`,
   keyed by url prefix. `HashMap::new()` still compiles; any non-empty map does
   not.
2. **`Repository::reopen`** — same change, wrapped in `Option<…>`.
3. **`virtual_chunks::mk_default_containers()` is removed**, and there are no
   default virtual chunk containers at all. Every container must be declared
   with `VirtualChunkContainer::new`.
4. **`VirtualChunkContainer` can no longer be written as a struct literal.**
   `url_prefix` is private; use `VirtualChunkContainer::new(url_prefix, store)`
   and read it back with `.url_prefix()`. `name` became
   `Option<ContainerName>`.
5. **`VirtualChunkContainer::validate_credentials`** takes
   `Option<&Credentials>` instead of `&Credentials`.
6. **`RepositoryConfig::virtual_chunk_containers`** is now
   `Option<HashMap<String, VirtualChunkContainer>>` and the key means url
   prefix, not container name. `get_virtual_chunk_container` keeps its name and
   changes its meaning to match.
7. **`VirtualChunkResolver::new`** takes
   `HashMap<String, Option<Credentials>>`, and silently drops every container
   that `VirtualChunkContainer::new` would now reject (§4.4) — a persisted
   configuration can carry one whose public `store` field no longer matches its
   url prefix. A dropped container claims nothing, so a url it would have
   claimed is `NoContainerForUrl`, not `UnauthorizedVirtualChunkContainer`,
   even when that prefix is in the authorisation map.
8. **`Session::merge(changes: ChangeSet)` → `Session::merge(other: Session)`.**
9. **Four `Storage` trait methods gained a leading `settings: &Settings`**:
   `delete_batch`, `get_object_range_buf`, `get_object_range_read` and
   `get_object_concurrently_multiple`. `Storage` is sealed, so this matters
   only to code that wraps a `Storage`.
   `ObjectStoreBackend::mk_object_store` likewise gained `&Settings`, and
   `ObjectStoreBackend` gained `can_write`.
10. **`ChangeSet`**: `chunk_changes` → `changed_chunks`; `take_chunks` and
    `set_chunks` are gone (splits are threaded through
    `set_chunk_ref(node, coord, data, &splits)`); `update_array` was
    re-signatured to take the splits.
11. **Error payloads are widely boxed**, so `matches!` patterns that bind a
    payload by value break:
    `SessionErrorKind::{NotAnArray, NotAGroup, AlreadyExists}` now carry
    `node: Box<NodeSnapshot>`; the `SerializationError` / `DeserializationError`
    variants of `SessionErrorKind`, `RepositoryErrorKind` and
    `IcechunkFormatErrorKind` carry `Box<…>`; and
    `StorageErrorKind::{ObjectStore, S3GetObjectError, S3PutObjectError,
    S3HeadObjectError, S3ListObjectError, S3DeleteObjectError, S3StreamError}`
    are all boxed.
12. **`initialize_tracing()` → `initialize_tracing(Option<&str>)`** (feature
    `logs`). `None` means "read the filter from the `ICECHUNK_LOG`
    environment variable"; a later call reloads the filter rather than
    installing a second subscriber.
13. **The first snapshot's id is fixed** (§4.1). Any assertion that two fresh
    repositories have different root snapshot ids is now false.
14. **New error variants**: `SessionErrorKind::{JsonSerializationError,
    InvalidIndexForSplitManifests, IncompatibleSplittingConfig,
    ManifestCreationError}`,
    `StorageErrorKind::{S3CreateMultipartUploadError, S3UploadPartError,
    S3CompleteMultipartUploadError}`,
    `VirtualReferenceErrorKind::UnauthorizedVirtualChunkContainer`.
15. **Two message strings changed** (`ManifestInfoNotFound`,
    `NoContainerForUrl`). Message wording is out of scope; the variants are
    unchanged.
16. **New workspace member `icechunk-macros`** (§1) — a dev-dependency of
    `icechunk`, not re-exported, out of scope.
17. **The crate version is `0.3.0`**, so `ICECHUNK_CLIENT_NAME` is `ic-0.3.0`.
18. **The on-disk spec version did not change** and must not be bumped
    (PROFILE.md P6-5).

---

## 6. The inherited contract, as this release spells it

Everything the previous five milestones established still holds. What follows
is the part the verifier re-checks, in this release's spelling.

### 6.0 Inherited symbols the verifier calls unchanged

These earlier milestones' items carry over **verbatim** — §5's migration list
does not touch them, so they must keep the names, paths and signatures they
already have. They are listed here because the verifier calls them, not because
anything about them is new:

```rust
// icechunk::session::Session
pub async fn add_group(&mut self, path: Path, definition: Bytes) -> SessionResult<()>;
pub async fn get_array(&self, path: &Path) -> SessionResult<NodeSnapshot>;
pub async fn get_group(&self, path: &Path) -> SessionResult<NodeSnapshot>;
pub async fn get_chunk_reader(&self, path: &Path, coords: &ChunkIndices,
                              byte_range: &ByteRange)
    -> SessionResult<Option<Pin<Box<dyn Future<Output = SessionResult<Bytes>> + Send>>>>;
pub fn get_chunk_writer(&self)
    -> impl FnOnce(Bytes) -> Pin<Box<dyn Future<Output = SessionResult<ChunkPayload>> + Send>>;

// icechunk::session — a free function, not a method
pub async fn get_chunk(
    reader: Option<Pin<Box<dyn Future<Output = SessionResult<Bytes>> + Send>>>,
) -> SessionResult<Option<Bytes>>;

// icechunk::storage::Storage — sealed; these are the listings the verifier uses
async fn list_chunks(&self, settings: &Settings)
    -> StorageResult<BoxStream<StorageResult<ListInfo<ChunkId>>>>;
async fn list_manifests(&self, settings: &Settings)
    -> StorageResult<BoxStream<StorageResult<ListInfo<ManifestId>>>>;
fn default_settings(&self) -> Settings;               // an owned value

// icechunk::storage::ListInfo — the verifier only counts these streams
pub struct ListInfo<Id> { pub id: Id, pub created_at: DateTime<Utc>, pub size_bytes: u64 }

// icechunk::session::Session — the whole tree of a version, as an iterator
pub async fn list_nodes(&self)
    -> SessionResult<impl Iterator<Item = SessionResult<NodeSnapshot>> + '_>;

// icechunk::format
pub struct ChunkIndices(pub Vec<u32>);                // public tuple field
impl ByteRange { pub const ALL: Self; }               // all bytes of the object
impl Path { pub fn root() -> Path; }                  // and `Path: TryFrom<&str>`
impl ManifestId { pub fn random() -> Self; }
pub type NodeId = ObjectId<8, NodeTag>;               // the id of one array or group
pub type ChunkOffset = u64;
pub type ChunkLength = u64;

// icechunk::format::manifest
pub enum ChunkPayload { Inline(Bytes), Virtual(VirtualChunkRef), Ref(ChunkRef) }
pub struct ManifestRef { pub object_id: ManifestId, pub extents: ManifestExtents }
pub struct VirtualChunkLocation(pub String);
impl VirtualChunkLocation {
    pub fn from_absolute_path(path: &str)
        -> Result<VirtualChunkLocation, VirtualReferenceError>;   // note the Result
}
pub struct VirtualChunkRef {
    pub location: VirtualChunkLocation,
    pub offset: ChunkOffset,
    pub length: ChunkLength,
    pub checksum: Option<Checksum>,
}
pub type VirtualReferenceError = ICError<VirtualReferenceErrorKind>;
pub enum VirtualReferenceErrorKind { /* §4.4 lists the two variants matched */ }

// icechunk::format::snapshot
pub enum NodeData { Array { shape, dimension_names, manifests }, Group }
pub struct NodeSnapshot {
    pub id: NodeId,
    pub path: Path,
    pub user_data: Bytes,
    pub node_data: NodeData,
}
```

`VirtualChunkLocation`, `VirtualChunkRef`, `VirtualReferenceError` and
`VirtualReferenceErrorKind` live in **`icechunk::format::manifest`**, not in
`icechunk::virtual_chunks`, even though §4.4 is where their behaviour is
stated. `NodeData` and `NodeSnapshot` live in **`icechunk::format::snapshot`**;
`ChunkPayload` and `ManifestRef` in **`icechunk::format::manifest`**. A
`NodeId` is never constructed by a caller: it is read off a `NodeSnapshot`,
which `Session::{get_array, get_group}` returns and `Session::list_nodes`
yields, and that is the value `Session::lookup_splits` (§3.10) takes.
`ManifestRef::extents` is a `ManifestExtents` (§3.5), so `is_empty()` on it is
that type's method.

Derives the verifier relies on and that are not printed with their types
elsewhere in this document: `RepositoryConfig: PartialEq`, `GCSummary:
PartialEq`, `VirtualChunkContainer: Clone`, `storage::Settings: Default +
Serialize + Deserialize` (a whole `RepositoryConfig`, `storage` field included,
round-trips through `serde_json` — §3.6), `DimensionName: From<&str>`, and
`conflicts::detector::ConflictDetector` being a **unit** struct (the verifier
passes `&ConflictDetector`). (`ManifestSplitCondition: Hash` is required by
§3.1's derive list, but no requirement group hashes one, so it is not listed here.)

### 6.1 Object layout and references (milestone 01)

A repository under a local-filesystem backend rooted at `$ROOT` uses exactly
these prefixes: `$ROOT/config.yaml`, `$ROOT/refs/`, `$ROOT/snapshots/`,
`$ROOT/manifests/`, `$ROOT/transactions/`, `$ROOT/chunks/`. **There is no
`$ROOT/attributes/`.** `config.yaml` appears once a configuration is persisted.

A branch is the single file `$ROOT/refs/branch.$NAME/ref.json`, holding JSON
whose only member is `snapshot`, the Crockford-base32 id. A tag is
`$ROOT/refs/tag.$NAME/ref.json`, written once and never updated; `delete_tag`
writes a sibling marker, so the tag disappears from `list_tags` and the **name
can never be used again** — a second `create_tag` with that name fails.

`Repository::list_branches` and `list_tags` return `BTreeSet<String>`, so they
are sorted.

Ids are 12 bytes rendered as 20 Crockford-base32 characters:
`SnapshotId::new([0,1,2,3,4,5,6,7,8,9,10,11])` is `"000G40R40M30E209185G"`, the
conversion round-trips, a short string is rejected, and `SnapshotId::random()`
does not collide.

### 6.2 Sessions, conflicts and rebase (milestone 02)

* `writable_session(branch)` gives a session with `read_only() == false` and
  `branch() == Some(branch)`; `readonly_session(&VersionInfo)` gives one with
  `read_only() == true` and `branch() == None`.
* A read-only session accepts edits into its change set but **cannot commit**:
  `SessionErrorKind::ReadOnlySession` (PROFILE.md P2-2).
* Two sessions opened at the same branch tip: the first to commit wins, and the
  second gets `SessionErrorKind::Conflict { expected_parent, actual_parent }`.
* `Session::rebase(&BasicConflictSolver::default())` on the loser, followed by
  a second `commit`, lands both changes.
* Virtual chunk containers are part of `RepositoryConfig`, are persisted by
  `save_config`, and are visible to a later `Repository::open` that authorises
  them.

### 6.3 Expiration and garbage collection (milestone 03)

```rust
// icechunk::ops::gc
pub fn GCConfig::clean_all(chunks_age: DateTime<Utc>, metadata_age: DateTime<Utc>,
                           extra_roots: Option<HashSet<SnapshotId>>) -> GCConfig;
pub async fn garbage_collect(storage: &(dyn Storage + Send + Sync),
                             storage_settings: &storage::Settings,
                             asset_manager: Arc<AssetManager>,
                             config: &GCConfig) -> GCResult<GCSummary>;
pub async fn expire(storage: &(dyn Storage + Send + Sync),
                    storage_settings: &storage::Settings,
                    asset_manager: Arc<AssetManager>,
                    older_than: DateTime<Utc>,
                    expired_branches: ExpiredRefAction,
                    expired_tags: ExpiredRefAction) -> GCResult<ExpireResult>;
pub struct GCSummary { pub bytes_deleted: u64, pub chunks_deleted: u64,
                       pub manifests_deleted: u64, pub snapshots_deleted: u64,
                       pub attributes_deleted: u64, pub transaction_logs_deleted: u64 }
pub struct ExpireResult { pub released_snapshots: HashSet<SnapshotId>,
                          pub edited_snapshots: HashSet<SnapshotId>,
                          pub deleted_refs: HashSet<Ref> }
pub enum ExpiredRefAction { Delete, Ignore }
```

* On a repository where nothing is unreachable, `garbage_collect` returns
  `GCSummary::default()` and deletes nothing.
* After `reset_branch` drops a commit, `garbage_collect` deletes exactly that
  commit's chunks, its manifest and its snapshot, and the surviving commit
  still reads correctly.
* `expire(older_than)` releases every snapshot in an ancestry older than the
  cutoff **except the repository root**, and re-parents the oldest survivor
  onto the root, so the branch's ancestry becomes `[survivor, root]`. Callers
  should reopen the repository afterwards to drop cached snapshots.

### 6.4 The FlatBuffers format and its header (milestone 04)

Every object under `snapshots/`, `manifests/` and `transactions/` begins with
the same 39 uncompressed bytes:

| bytes | content |
| --- | --- |
| 0–11 | the UTF-8 magic `ICE🧊CHUNK` (`49 43 45 f0 9f a7 8a 43 48 55 4e 4b`) |
| 12–35 | the writing client's id, UTF-8, right-padded with ASCII spaces to exactly 24 bytes: `"ic-"` + the crate version |
| 36 | spec version — **`1`**, unchanged at this release (PROFILE.md P6-5) |
| 37 | file type: `1` snapshot, `2` manifest, `3` attributes, `4` transaction log |
| 38 | compression algorithm: `0` none, `1` zstd — metadata objects are written as `1` |
| 39.. | the compressed FlatBuffers payload |

Reference files and `config.yaml` carry no header; chunk objects carry no
header.

The same objects must decode through the public API: `lookup_snapshot` returns
the commit's id, message, parent, `flushed_at` and manifest list, and
`Session::list_nodes` yields every node path in the snapshot.

### 6.5 The command-line interface (milestone 05)

The binary is `icechunk`, built from `/app/icechunk` with `--features cli`. The
verifier passes its path in **`ICECHUNK_TEST_CLI_BIN`**.

Argument grammar — unchanged at this release:

```
icechunk repo create <alias>
icechunk snapshot list <REPO> [-n <N>] [-b|--branch <BRANCH>]
icechunk config init [-f|--force]
icechunk config add <alias>
icechunk config list
icechunk help [SUBCOMMAND...]
```

`-n` defaults to 10, `-b/--branch` defaults to `main`. There is no
`--version`.

Configuration is read from `dirs::config_dir()/icechunk/cli-config.yaml`, i.e.
`$XDG_CONFIG_HOME/icechunk/cli-config.yaml` on Linux. The file is an
externally-tagged YAML enum (PROFILE.md P5-2):

```yaml
repos:
  local1: !LocalFileSystem
    path: /some/path
    config: {}
```

Behaviour the verifier relies on:

* `icechunk --help` and the `icechunk help` subcommand both exit `0` and list
  the `repo`, `snapshot` and `config` commands on stdout. Running with no
  subcommand, or with an unknown subcommand, exits non-zero.
* `icechunk config list` exits `0` and writes the configuration the binary
  loaded to stdout, so a configured alias appears in it. (`config init` and
  `config add` are interactive and are out of scope — PROFILE.md P5-3.)
* `icechunk repo create <alias>` creates a real Icechunk repository at the
  configured path — `refs/branch.main/ref.json` and `snapshots/` appear, and
  the library can open it and find `Snapshot::INITIAL_SNAPSHOT_ID`. It exits
  `0` and prints one line to stdout.
* `icechunk snapshot list <alias>` exits `0` and prints **one line per
  snapshot, newest first**, each containing that snapshot's commit message.
  `-n` bounds the number of lines.
* An alias absent from the configuration, or a branch absent from the
  repository, exits non-zero.

Exact stdout wording is **out of scope**; line counts, the presence of the commit
message, and exit statuses are.

---

## 7. What is out of scope

None of the following is required, and no requirement group asserts any of it.

* **The PyO3-only operator spellings.** `ManifestSplitCondition.and_conditions`
  / `.or_conditions` / `&` / `|`, the same four on
  `ManifestPreloadCondition`, `ManifestSplittingConfig.from_dict`,
  `ManifestPreloadCondition.num_refs(a, b)`,
  `Session.commit(rebase_with=…)`, `Repository.transaction`,
  `Session.fork` / `ForkSession`, `icechunk.set_logs_filter`,
  `StorageSettings` / `StorageRetriesSettings`, and
  `s3_refreshable_credentials(scatter_initial_credentials=True)`. They are
  Python-package surface. The Rust equivalents in §§3 and 4 are what is in scope;
  see PROFILE.md's "Python spellings" table.
* **HTTP virtual chunks.** `ObjectStoreConfig::Http` must exist and
  `VirtualChunkContainer::new` must validate an `http(s)://` prefix against it,
  but nothing fetches a chunk over HTTP: the verifier has no network and starts
  no server. Virtual chunks are in scope over `file://` containers only
  (PROFILE.md P6-9).
* **Retry behaviour.** `RetriesSettings` covers construction, defaults,
  `merge`, the in-memory backend's override and propagation into
  `Repository::storage_settings()`. Whether a failed request is actually
  retried, how long it backs off, and the hard-coded five-minute
  `retry_timeout` are not — exercising them needs a failing remote endpoint.
* **Manifest preloading's effect.** Whether a manifest was actually fetched
  into cache is not observable from outside the crate
  (`storage::logging::LoggingStorage::new` is `#[cfg(test)]`) and is a
  background task besides. The preload *predicate* and the preload
  *configuration accessors* are in scope; the fetch is not (PROFILE.md P6-4).
* **`design/005-manifest-split.md`.** It describes a design that was never
  implemented, and none of its identifiers — manifest sets,
  `max-manifest-size`, `arrays-per-manifest`, `overflow-to`, `cardinality`,
  bin-packing, `metadata-chunks` — exists in the released code or in this
  contract. Nothing is checked against it (PROFILE.md P6-6).
* **A spec-version bump.** Byte 36 stays `1`. "Format 1.0 frozen" is a
  compatibility policy in the documentation, not a byte change (PROFILE.md
  P6-5).
* **`icechunk-macros`.** It must exist as a workspace member so the workspace
  builds, but nothing in this milestone uses `#[tokio_test]` or
  `#[icechunk_macros::test]`, and it is not re-exported.
* **Three of the four new `SessionErrorKind` variants ever being *returned*.**
  `InvalidIndexForSplitManifests`, `JsonSerializationError` and
  `ManifestCreationError` must exist with the payloads §3.10 gives them, and
  that is all: no public call in this release reaches any of them, and the enum
  is `#[non_exhaustive]`, so nothing outside the crate can build one either. No
  group asserts that any of the three is produced. The fourth,
  `IncompatibleSplittingConfig`, **is** in scope — §4.2's grid-mismatched
  `Session::merge` produces it.
* **A grid queried with a coordinate of the wrong dimensionality.** §3.4's
  `find` and `position` are documented with the coordinate's dimensionality as
  a **precondition**. The release neither checks it nor rejects it, so there is
  no defined outcome to check, and no group asserts one. (`ManifestSplits::from_extents`
  refusing an **empty** grid *is* in scope — that one the release does assert.)
* **Internal layout, private names, module organisation, allocation, timing,
  log output and error-message wording.** Errors are matched on their public
  variant, never on their `Display` text.
* **The candidate's own tests.** They are never run and never determine
  acceptance.
* **Cloud and MinIO paths.** S3, GCS, Azure, Tigris, multipart upload, storage
  classes and anonymous public-bucket access are outside this milestone's
  scope; the types must exist where §4.3 says so, but nothing connects
  to a remote endpoint.
