# TEST_INTERFACE.md — milestone 03 (expiration, garbage collection, file headers)

The public contract the hidden verifier compiles against. It is self-contained:
everything the verifier needs — crate identity, build command, module paths,
signatures, constants, byte layouts, error categories, defaults, and the
migration from milestone 2 — is either written here or in a document
`instruction.md` sends you to.

The verifier links against your crate **by path** from a throw-away driver crate
outside `/app`. It only ever uses the items named in this file. It never reads
your source, never runs your tests, and never edits your manifests.

Section 12 lists what is **out of scope**.

---

## 1. Crate identity and build contract

| | |
| --- | --- |
| Workspace root | `/app`, with `default-members = ["icechunk"]` |
| Library crate | `icechunk` at `/app/icechunk`, `edition = "2021"` |
| Version | `0.1.0` (was `0.1.0-alpha.8`) |
| Build command | `cargo build` run in `/app` must succeed |
| Linkage | the driver declares `icechunk = { path = "/app/icechunk" }` |
| Lockfile | `/app/Cargo.lock` must exist, be a regular file, and be consistent with the manifests |

`ICECHUNK_LIB_VERSION` is `env!("CARGO_PKG_VERSION")`, so the package version in
`/app/icechunk/Cargo.toml` is what ends up inside every metadata file's header
(section 5). Set it to `0.1.0`.

The workspace lint table (`[workspace.lints.clippy]` with `expect_used`,
`unwrap_used`, `panic`, `todo`, `unimplemented` all at `warn`) is unchanged from
milestone 2 and is out of scope either way.

## 2. Workspace layout

```
/app
├── Cargo.toml            # workspace; default-members = ["icechunk"]
├── Cargo.lock
├── icechunk/
│   ├── Cargo.toml        # name = "icechunk", version = "0.1.0"
│   └── src/…             # cumulative implementation
├── icechunk-spec/        # read-only specification corpus (never edit)
├── TEST_INTERFACE.md     # this file
├── WORKSPACE_HANDOFF.md
└── timer.sh
```

Do not move the crate, rename the package, or add a second workspace member the
verifier would have to discover.

## 3. Crates the verifier links against

The driver crate pins these and nothing else. Any type below that crosses your
public API must come from the same major version.

| crate | version the driver asks for | what crosses the boundary |
| --- | --- | --- |
| `bytes` | `1`, features `["serde"]` | `Bytes` in `ChunkPayload::Inline`, `Storage::fetch_chunk`, `AssetManager::write_chunk` |
| `chrono` | `0.4`, features `["serde"]` | `DateTime<Utc>` in `expire`, `expire_ref`, `GCConfig`, `Action`, `SnapshotInfo::flushed_at` |
| `futures` | `0.3` | `Stream` / `BoxStream` returned by `ancestry`, `chunk_coordinates`, `list_*` |
| `serde_json` | `1` | `serde_json::Value`, the value type of `SnapshotProperties` |
| `tempfile` | `3` | verifier-side only |
| `tokio` | `1`, features `["rt-multi-thread","macros","fs","sync","time","io-util"]` | `tokio::io::AsyncRead` in the `Storage::fetch_*` return types |

The drivers are `#[tokio::test]` async tests. `Storage::fetch_snapshot`,
`fetch_manifest_unknown_size` and `fetch_transaction_log` must return something
the verifier can `AsyncReadExt::read_exact` from — i.e. a
`Box<dyn tokio::io::AsyncRead + Unpin + Send>`.

## 4. What this milestone adds

In one sentence each; the signatures follow in sections 5–10.

1. A **39-byte binary header** on every snapshot, manifest, attributes and
   transaction-log file, and **Zstd compression** of their MessagePack payloads.
2. **Expiration**: rewriting history so snapshots older than a caller-supplied
   cutoff drop out of every branch's and tag's ancestry.
3. **Garbage collection**: deleting exactly the objects no branch or tag can
   reach, and reporting five per-category counts.
4. **Tag deletion**, with the deleted name permanently unusable.
5. **Refusing to create a repository in a non-empty prefix.**
6. **Arbitrary commit properties**, readable back through `ancestry`.
7. **`Session::chunk_coordinates`**, listing an array's initialized chunks.
8. **One manifest per array**, each recorded in the snapshot with its
   **chunk-space bounding box**, size in bytes and row count.
9. **Field-by-field configuration merge** over the persisted configuration.
10. The **`asset_manager`** module, which replaces milestone 2's
    `MemCachingStorage` (section 11).

Everything milestones 1 and 2 established stays required. Section 10 states the
inherited contract in this release's spelling; section 11 is the migration.

## 5. The on-disk metadata file header — `icechunk::format::format_constants`

Normative source: `icechunk-spec/design/004-binary-format.md`. `spec.md` does
**not** describe it — see `icechunk-spec/PROFILE.md` §D1.

```rust
pub mod format_constants {
    #[repr(u8)] #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub enum FileTypeBin { Snapshot = 1, Manifest = 2, Attributes = 3, TransactionLog = 4 }
    impl TryFrom<u8> for FileTypeBin { type Error = String; }

    #[repr(u8)] #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub enum SpecVersionBin { V0dot1 = 1 }
    impl TryFrom<u8> for SpecVersionBin { type Error = String; }
    impl SpecVersionBin { pub fn current() -> Self; }

    #[repr(u8)] #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub enum CompressionAlgorithmBin { None = 0, Zstd = 1 }
    impl TryFrom<u8> for CompressionAlgorithmBin { type Error = String; }

    pub const ICECHUNK_FORMAT_MAGIC_BYTES: &[u8];  // 12 bytes
    pub const ICECHUNK_LIB_VERSION: &str;          // env!("CARGO_PKG_VERSION")
    pub static ICECHUNK_CLIENT_NAME: LazyLock<String>;  // "ic-" + ICECHUNK_LIB_VERSION
}
```

`FileTypeBin` also carries a `Chunk` variant upstream; the verifier never reads
it and its discriminant is out of scope, except that it must not be `9` — see the
next paragraph. The four above are in scope.

`TryFrom<u8>` must **reject** a value with no variant: `SpecVersionBin::try_from(0)`
and `FileTypeBin::try_from(9)` are both errors. (Byte `0x00` is reserved for a
future spec version, so it is not a readable one; `9` is simply a byte no file
type uses, so whatever further variants `FileTypeBin` carries, none of them may
take the value `9`.)

### 5.1 The byte layout

`ICECHUNK_FORMAT_MAGIC_BYTES` is `"ICE🧊CHUNK"` in UTF-8, i.e. exactly

```
49 43 45 F0 9F A7 8A 43 48 55 4E 4B
```

Every snapshot, manifest and transaction-log object written by a commit starts
with these 39 bytes, in this order:

| offset | width | content |
| --- | --- | --- |
| 0..12 | 12 | the magic bytes above |
| 12..36 | 24 | the writing client's id, UTF-8, **right-padded with ASCII spaces (`0x20`)** to exactly 24 bytes. The official client writes `format!("{:<24}", ICECHUNK_CLIENT_NAME)`, so at this version the field is `"ic-0.1.0"` followed by 16 spaces. The verifier asserts only that it is valid UTF-8, 24 bytes, begins `"ic-"`, and that every byte after the non-blank prefix is `0x20`. |
| 36 | 1 | `SpecVersionBin as u8` — `0x01` at this release |
| 37 | 1 | `FileTypeBin as u8` — `0x01` snapshot, `0x02` manifest, `0x03` attributes, `0x04` transaction log |
| 38 | 1 | `CompressionAlgorithmBin as u8` — `0x01` (Zstd) for all three metadata kinds at this release |
| 39.. | — | the compressed MessagePack payload. **The header is never compressed**, so byte 39 is the first byte of the Zstandard frame and reads `28 B5 2F FD`. |

### 5.2 What carries no header

- **Chunk objects** are written byte-for-byte as they were handed in. A chunk
  fetched back through `Storage::fetch_chunk` equals the bytes that were
  written, and never begins with the magic bytes.
- **Reference files** (`refs/branch.$NAME/$SEQ.json`, `refs/tag.$NAME/ref.json`)
  stay pure JSON.
- **The configuration file** (`config.yaml` at the repository root) stays plain
  text and is readable back through `Repository::fetch_config`.

### 5.3 Object-store metadata

Where the backend supports it, the same facts are also written as object-store
metadata alongside each metadata file, using the keys `ic_spec_ver`,
`ic_client`, `ic_file_type` and `ic_comp_alg`. It is written but not consulted
on read, and the verifier does not read it back — see section 12.

### 5.4 Compression, and refusing a header that does not check out

`RepositoryConfig::compression()` selects the algorithm and level (section 9).
The only algorithm is `CompressionAlgorithm::Zstd` and the default level is `1`.
Changing the level must not change byte 38 and must not stop the object from
round-tripping through the typed reader.

**A metadata object whose header does not check out must be rejected, not
misparsed.** That is what the header is for. Reading a snapshot, manifest or
transaction-log object through the typed path (`AssetManager::fetch_snapshot`,
`fetch_manifest_unknown_size`, `fetch_manifest`, `fetch_transaction_log`) must
**fail** when the first 39 bytes are not the ones section 5.1 describes — the
magic bytes are not icechunk's, the spec-version byte has no `SpecVersionBin`
variant, the file-type byte is not the kind being read, or the compression byte
has no `CompressionAlgorithmBin` variant — even when the payload behind the
header is otherwise perfectly valid. Those four are the four ways the verifier
doctors a header: it rewrites one real object with exactly one of its first 39
bytes changed, requires the typed read to fail, and then puts the original bytes
back and requires the same object to read.

`IcechunkFormatError` gained these variants; they are the error *categories* a
corrupt header produces, and the verifier does not assert their messages:
`InvalidMagicNumbers`, `InvalidSpecVersion`,
`InvalidFileType { expected: FileTypeBin, got: u8 }`,
`InvalidCompressionAlgorithm`, `ManifestInfoNotFound { manifest_id: ManifestId }`.
Which of them a given corruption reports, and how it is wrapped on the way out
of `AssetManager`, is **out of scope**: the verifier asserts only that the read
fails (`PROFILE.md` §P6).

## 6. Expiration and garbage collection — `icechunk::ops::gc`

Normative source: `icechunk-spec/design/007-basic-expiration.md`.

```rust
#[derive(Debug, PartialEq, Eq)]
pub enum Action { Keep, DeleteIfCreatedBefore(chrono::DateTime<chrono::Utc>) }

#[derive(Debug)]
pub struct GCConfig { /* private fields */ }

impl GCConfig {
    pub fn new(
        extra_roots: std::collections::HashSet<SnapshotId>,
        dangling_chunks: Action,
        dangling_manifests: Action,
        dangling_attributes: Action,
        dangling_transaction_logs: Action,
        dangling_snapshots: Action,       // <- 6th parameter, new at this milestone
    ) -> Self;

    pub fn clean_all(
        chunks_age: DateTime<Utc>,
        metadata_age: DateTime<Utc>,
        extra_roots: Option<HashSet<SnapshotId>>,
    ) -> Self;                            // every category set to DeleteIfCreatedBefore

    pub fn deletes_chunks(&self) -> bool;
    pub fn deletes_manifests(&self) -> bool;
    pub fn deletes_attributes(&self) -> bool;
    pub fn deletes_transaction_logs(&self) -> bool;
    pub fn deletes_snapshots(&self) -> bool;
}

#[derive(Debug, PartialEq, Eq, Default)]
pub struct GCSummary {
    pub chunks_deleted: usize,
    pub manifests_deleted: usize,
    pub snapshots_deleted: usize,
    pub attributes_deleted: usize,
    pub transaction_logs_deleted: usize,
}

#[derive(Debug, thiserror::Error)]
pub enum GCError {
    Ref(#[from] RefError),
    Storage(#[from] StorageError),
    Repository(#[from] RepositoryError),
    FormatError(#[from] IcechunkFormatError),
}
pub type GCResult<A> = Result<A, GCError>;

pub async fn garbage_collect(
    storage: &(dyn Storage + Send + Sync),
    storage_settings: &icechunk::storage::Settings,
    asset_manager: Arc<AssetManager>,
    config: &GCConfig,
) -> GCResult<GCSummary>;

pub async fn expire_ref(
    storage: &(dyn Storage + Send + Sync),
    storage_settings: &icechunk::storage::Settings,
    asset_manager: Arc<AssetManager>,
    reference: &icechunk::refs::Ref,
    older_than: DateTime<Utc>,
) -> GCResult<HashSet<SnapshotId>>;

pub async fn expire(
    storage: &(dyn Storage + Send + Sync),
    storage_settings: &icechunk::storage::Settings,
    asset_manager: Arc<AssetManager>,
    older_than: DateTime<Utc>,
) -> GCResult<HashSet<SnapshotId>>;
```

There is **no `ExpireResult` and no `ExpiredRefAction`** at this release, and
`GCSummary` carries **no bytes-freed figure**: five `usize` counts, nothing else.

### 6.1 Expiration semantics the verifier asserts

- `expire_ref` walks only the ancestry of the one `Ref` it is given. Other
  branches and tags keep the ancestry they had.
- It rewrites the **oldest non-expired snapshot in place** — same `SnapshotId`,
  same object path — re-parenting it directly to the repository root. After the
  call, that snapshot's `parent_id` is the root's id.
- `expire` does the same for every reference in the repository — **every branch
  and every tag**, not the branches alone. A snapshot that only a tag's ancestry
  reaches is expired, reported and rewritten exactly as one on a branch would
  be. Processing order does not change the end state: expiring ref by ref in any
  order and calling `expire` once leave every branch and tag with the same
  history.
- A snapshot a branch points at directly is never skipped from history, and
  every branch and tag still resolves to exactly the snapshot it resolved to
  before the call.
- The root snapshot is never reported as released.
- If the reference already points at a snapshot older than the cutoff, the call
  does nothing and returns an **empty** set.
- If the oldest kept snapshot *is* the root, or already has the root as its
  parent, there is nothing to rewrite and no snapshot is written; the call still
  reports whatever ids were skipped from history, which for a repository with
  only the root commit is an empty set.
- The returned set is the ids **skipped from history**. Those are *not
  necessarily collectable*: a tag may still point into that set, in which case
  the tag keeps resolving and keeps exposing its own (now outdated) ancestry.
- Expiration **deletes no object**. Object counts under every prefix are
  unchanged by `expire` / `expire_ref`.
- A snapshot's own timestamp is `SnapshotInfo::flushed_at`, and the comparison
  is `flushed_at >= older_than` ⇒ keep. An in-place rewrite keeps the original
  timestamp.

Because `expire` rewrites a snapshot at its existing id, a cached copy of that
snapshot is stale afterwards. The verifier obtains a fresh view by constructing
a new `AssetManager` (or by `Repository::open`), which is the documented way —
there is no public cache-invalidation call.

### 6.2 Garbage-collection semantics the verifier asserts

- GC deletes exactly the objects unreachable from every branch and tag (plus
  `extra_roots`), and never one that is reachable. A tag alone is enough to
  keep a snapshot, its manifests and its chunks alive.
- `extra_roots` is a root set of the same standing as the references: a snapshot
  id in it, and everything its ancestry reaches, survives collection even when
  no branch and no tag points anywhere near it. It is the third argument of
  `GCConfig::clean_all` (`None` means "no extra roots") and the first of
  `GCConfig::new`.
- When a snapshot becomes unreachable, its **exclusive** manifests, its
  **exclusive** chunks and its transaction log go with it — in particular the
  chunk objects a later, now-forgotten commit wrote to overwrite earlier ones.
  A transaction log is keyed by the id of the snapshot that produced it, so it
  is collected exactly when that snapshot is.
- With nothing dangling, the result equals `GCSummary::default()` and no object
  is removed. A second run straight after a collecting run is also a
  `GCSummary::default()`.
- Each category is selected independently. `Action::Keep` for a category means
  its `deletes_*()` getter is `false`, its count stays `0`, and no object of
  that kind is removed; a `GCConfig` whose five actions are all `Keep` deletes
  nothing at all.
- `attributes_deleted` is `0` at this release, because attribute files are not
  produced.
- After collection the repository still opens, its branch still resolves, and
  the surviving chunks still read back byte-for-byte.

## 7. Tag deletion — `icechunk::refs` and `Repository`

Normative source: `icechunk-spec/design/006-tag-delete.md`; see also
`icechunk-spec/guide/icechunk-python-version-control.md` → "## Tags".
`icechunk-spec/spec.md` still says tags cannot be deleted; `PROFILE.md` §D2 and
§R2 resolve that contradiction in favour of the design document.

```rust
pub enum Ref { Tag(String), Branch(String) }  // + PartialOrd, Ord
impl Ref {
    pub const DEFAULT_BRANCH: &'static str = "main";
    pub fn is_tag(&self) -> bool;
    pub fn is_branch(&self) -> bool;
    pub async fn fetch(&self, storage: &(dyn Storage + Send + Sync),
                       storage_settings: &storage::Settings) -> RefResult<RefData>;
}

pub struct RefData { pub snapshot: SnapshotId }   // what `Ref::fetch` resolves to

pub async fn delete_tag(storage: &(dyn Storage + Send + Sync),
                        storage_settings: &storage::Settings,
                        tag: &str, overwrite_refs: bool) -> RefResult<()>;

impl Repository { pub async fn delete_tag(&self, tag: &str) -> RepositoryResult<()>; }
```

Behaviour the verifier asserts, through `Repository`:

- Deleting a tag is a **logical delete**: a tombstone object is created beside
  the live reference and the live reference is left in place. The two keys are
  the ones `design/006-tag-delete.md` → "## Design" names —
  `refs/tag.$NAME/ref.json` stays, and the empty
  `refs/tag.$NAME/ref.json.deleted` appears next to it. Both are observable
  through `ObjectStorage::all_keys()` (section 9.5).
- A deleted tag is **hidden from `list_tags`**.
- A deleted tag **does not resolve**: `lookup_tag` fails with the error category
  `RepositoryError::Ref(RefError::RefNotFound(_))`, and
  `readonly_session(&VersionInfo::TagRef(..))` fails with it.
- Deleting the same tag twice fails, and so does deleting a name that never
  existed. A failed delete changes nothing.
- Other tags are unaffected by a delete.
- **The name can never be reused.** `create_tag` on a deleted name fails,
  whether or not it points at the same snapshot, with the error category
  `RepositoryError::Ref(RefError::TagAlreadyExists(name))` where `name` is the
  tag name. An unused name still works, and a live tag is still immutable.

`Ref::fetch` resolves a reference to the snapshot it points at — for a branch,
its tip. `Ref`'s derived `Ord` orders two references of the same kind by name;
how it orders a tag against a branch is out of scope.

`RefError::TagAlreadyExists(String)` and `RefError::RefNotFound(String)` are the
variants the verifier matches on; their message text is out of scope.

## 8. Repository — `icechunk::repository`

```rust
pub async fn create(config: Option<RepositoryConfig>,
                    storage: Arc<dyn Storage + Send + Sync>,
                    virtual_chunk_credentials: HashMap<ContainerName, Credentials>)
    -> RepositoryResult<Self>;
pub async fn open(/* same three */) -> RepositoryResult<Self>;
pub async fn open_or_create(/* same three */) -> RepositoryResult<Self>;
pub async fn exists(storage: &(dyn Storage + Send + Sync)) -> RepositoryResult<bool>;

// new at this milestone
pub fn reopen(&self, config: Option<RepositoryConfig>,
              virtual_chunk_credentials: Option<HashMap<ContainerName, Credentials>>)
    -> RepositoryResult<Self>;
pub fn storage_settings(&self) -> &icechunk::storage::Settings;
pub fn asset_manager(&self) -> &Arc<AssetManager>;
pub async fn snapshot_ancestry(&self, snapshot_id: &SnapshotId)
    -> RepositoryResult<impl Stream<Item = RepositoryResult<SnapshotInfo>> + '_>;
pub async fn snapshot_ancestry_arc(self: Arc<Self>, snapshot_id: &SnapshotId)
    -> RepositoryResult<impl Stream<Item = RepositoryResult<SnapshotInfo>>>;
pub async fn ancestry_arc(self: Arc<Self>, version: &VersionInfo)
    -> RepositoryResult<impl Stream<Item = RepositoryResult<SnapshotInfo>>>;
pub async fn delete_tag(&self, tag: &str) -> RepositoryResult<()>;

// changed at this milestone
pub async fn ancestry(&self, version: &VersionInfo)
    -> RepositoryResult<impl Stream<Item = RepositoryResult<SnapshotInfo>> + '_>;
pub async fn list_branches(&self) -> RepositoryResult<BTreeSet<String>>;
pub async fn list_tags(&self) -> RepositoryResult<BTreeSet<String>>;
pub async fn delete_branch(&self, branch: &str) -> RepositoryResult<()>;  // errors on "main"
pub async fn save_config(&self) -> RepositoryResult<ETag>;

// unchanged
pub async fn create_branch(&self, branch_name: &str, snapshot_id: &SnapshotId) -> RepositoryResult<BranchVersion>;
pub async fn lookup_branch(&self, branch: &str) -> RepositoryResult<SnapshotId>;
pub async fn reset_branch(&self, branch: &str, snapshot_id: &SnapshotId) -> RepositoryResult<BranchVersion>;
pub async fn create_tag(&self, tag_name: &str, snapshot_id: &SnapshotId) -> RepositoryResult<()>;
pub async fn lookup_tag(&self, tag: &str) -> RepositoryResult<SnapshotId>;
pub async fn readonly_session(&self, version: &VersionInfo) -> RepositoryResult<Session>;
pub async fn writable_session(&self, branch: &str) -> RepositoryResult<Session>;
pub async fn fetch_config(storage: &(dyn Storage + Send + Sync))
    -> RepositoryResult<Option<(RepositoryConfig, ETag)>>;
pub fn config(&self) -> &RepositoryConfig;
pub fn storage(&self) -> &Arc<dyn Storage + Send + Sync>;

#[non_exhaustive]
pub enum VersionInfo { SnapshotId(SnapshotId), TagRef(String), BranchTipRef(String) }
```

`Repository` derives `Debug, Serialize, Deserialize`.

`storage()` hands back a handle to the repository's own backing store — whether
it is literally the `Arc` that was passed to `create` is out of scope, only that
reading through it reaches the same objects. `storage_settings()` is the
`Settings` the repository actually passes to that storage: the configuration's
own `storage` settings when it sets them (section 9.4), and the backend's
`default_settings()` when it does not.

`delete_branch(name)` removes a branch: afterwards the name is gone from
`list_branches` and `lookup_branch` fails for it. **The default branch is the
exception** — `delete_branch("main")` (`Ref::DEFAULT_BRANCH`) must be **refused**
with `RepositoryError::CannotDeleteMain`, and the refusal must leave `main`
listed and resolving exactly as it was.

`RepositoryError` (`#[non_exhaustive]`) — the variants the verifier matches on
by name are `ParentDirectoryNotClean`, `Ref(RefError)` and `CannotDeleteMain`.
Also present: `IOError(#[from] std::io::Error)`,
`ConcurrencyError(#[from] tokio::task::JoinError)`. `AlreadyInitialized` is
gone.

### 8.1 Creating in a clean prefix only

`Repository::create` must fail with `RepositoryError::ParentDirectoryNotClean`
when the storage prefix already contains anything. This replaces milestone 2's
"the default branch already exists" test, and the trigger is different: it is
now "the prefix is non-empty", so creating a repository rooted at a *sub-prefix*
of an existing repository (for example the `snapshots/` directory of one) fails
too. `open`, `open_or_create` and `exists` on the existing repository keep
working, and a genuinely empty prefix still accepts a new repository.

### 8.2 Ancestry and commit metadata

`Session::commit` takes an optional property map, and those properties come back
through `ancestry`:

Both types live in `icechunk::format::snapshot`, beside `Snapshot`,
`ZarrArrayMetadata` and `NodeData`:

```rust
// icechunk::format::snapshot
pub type SnapshotProperties = std::collections::HashMap<String, serde_json::Value>;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SnapshotInfo {
    pub id: SnapshotId,
    pub parent_id: Option<SnapshotId>,
    pub flushed_at: DateTime<Utc>,
    pub message: String,
    pub metadata: SnapshotProperties,
}
```

- `ancestry` yields **latest first** and **includes** the snapshot it starts at.
- It accepts all three `VersionInfo` forms, and all three produce the same list
  when they resolve to the same snapshot. `snapshot_ancestry(&SnapshotId)` is
  the same walk reached without a `VersionInfo`.
- Every entry's `parent_id` is the next entry's `id`; the last entry's
  `parent_id` is `None`.
- `flushed_at` is strictly decreasing down the chain — a snapshot's timestamp is
  strictly greater than its parent's. The rule is enforced by **refusing the
  commit** — `SessionError::InvalidSnapshotTimestampOrdering { parent, child }`,
  nothing written — and never by adjusting the child's timestamp to sit above
  its parent's. A commit's `flushed_at` is the time that commit was flushed.
- A commit made with no properties has an **empty** metadata map.
- The repository's first snapshot has message `"Repository initialized"`
  (`Snapshot::INITIAL_COMMIT_MESSAGE`) and carries the property key `"__root"`.
- Properties survive a round trip through storage: they are readable from a
  freshly `open`ed `Repository`.

## 9. Sessions, manifests and configuration

### 9.1 `icechunk::session::Session`

```rust
// new at this milestone
pub async fn chunk_coordinates<'a, 'b: 'a>(&'a self, array_path: &'b Path)
    -> SessionResult<impl Stream<Item = SessionResult<ChunkIndices>> + 'a>;
pub async fn delete_node(&mut self, node: NodeSnapshot) -> SessionResult<()>;
pub async fn get_closest_ancestor_node(&self, path: &Path) -> SessionResult<NodeSnapshot>;
pub fn matching_container(&self, chunk_location: &VirtualChunkLocation) -> Option<&VirtualChunkContainer>;

// unchanged shape
pub async fn commit(&mut self, message: &str, properties: Option<SnapshotProperties>)
    -> SessionResult<SnapshotId>;
pub fn branch(&self) -> Option<&str>;
pub fn read_only(&self) -> bool;
pub fn snapshot_id(&self) -> &SnapshotId;
pub fn has_uncommitted_changes(&self) -> bool;
pub fn as_bytes(&self) -> SessionResult<Vec<u8>>;
pub fn from_bytes(bytes: Vec<u8>) -> SessionResult<Self>;
```

`chunk_coordinates` yields every **initialized** chunk coordinate of the named
array — the committed ones plus anything the session itself has staged — in no
particular order, with no duplicates. An array with no initialized chunks yields
nothing. A path that is not an array is an error (`SessionError::NotAnArray`).

`SessionError` gained `AncestorNodeNotFound { prefix: Path }`,
`InvalidSnapshotTimestampOrdering { parent, child }`,
`InvalidSnapshotTimestamp { object_store_time, snapshot_time }` and
`ConcurrencyError(#[from] JoinError)`.

### 9.2 One manifest per array, with a bounding box

Normative source: `icechunk-spec/design/005-manifest-split.md`.

A commit that touches *N* arrays writes *N* manifest objects — one per array,
not one shared manifest. Each array's `NodeData::Array(_, Vec<ManifestRef>)`
then names exactly one manifest.

`Manifest`, `ManifestRef` and `ChunkInfo` live in `icechunk::format::manifest`,
beside `ChunkPayload` and `VirtualChunkRef`; `ManifestFileInfo` lives in
`icechunk::format::snapshot`, beside `Snapshot`; `ManifestId` is
`icechunk::format::ManifestId`.

```rust
// icechunk::format::manifest
pub struct ManifestRef {
    pub object_id: ManifestId,
    pub extents: std::ops::Range<ChunkIndices>,   // was ManifestExtents(Vec<ChunkIndices>)
}
```

`extents` is the **chunk-space bounding box** of the chunks in that manifest:
`start` is the per-axis minimum coordinate, and `end` is the per-axis maximum
**plus one** — the end is exclusive. For chunks at `(1,2)`, `(3,4)` and `(2,7)`
the extent is `ChunkIndices([1,2]) .. ChunkIndices([4,8])`.

The snapshot records one `ManifestFileInfo` per manifest:

```rust
pub struct ManifestFileInfo { pub id: ManifestId, pub size_bytes: u64, pub num_rows: u32 }
```

reachable from `Snapshot` through `manifest_files() -> &HashMap<ManifestId, ManifestFileInfo>`,
`manifest_info(&ManifestId) -> Option<&ManifestFileInfo>` and
`get_manifest_file(&ManifestId) -> Option<&ManifestFileInfo>`. `num_rows` is the
number of chunk references in that manifest; `size_bytes` is the written object
size and must be non-zero.

`Snapshot` is otherwise opaque — all fields private, with accessors `id()`,
`parent_id()`, `metadata()`, `flushed_at()`, `message()`, `nodes()`,
`attribute_files()`, `get_node(&Path)`, `len()`, `is_empty()`, `iter()` — plus
the expiration primitive `pub fn adopt(&self, parent: &Snapshot) -> Self` and
the constructor `pub fn initial() -> Self`.

`Manifest` gained `pub id: ManifestId` and
`pub fn chunk_payloads(&self) -> impl Iterator<Item = &ChunkPayload>`;
`Manifest::chunks()` is gone and `FromIterator<ChunkInfo>` was replaced by the
async `from_iter` / `from_stream`, which return `Option` (an empty stream yields
`None` and writes no manifest):

```rust
impl Manifest {
    pub async fn from_stream<E>(stream: impl futures::Stream<Item = Result<ChunkInfo, E>>)
        -> Result<Option<Self>, E>;
    pub async fn from_iter<T: IntoIterator<Item = ChunkInfo>>(iter: T)
        -> Result<Option<Self>, std::convert::Infallible>;
}
```

`TransactionLog` gained `len()` and `is_empty()`.

### 9.3 `icechunk::asset_manager::AssetManager`

New public module, and the typed read/write path the verifier uses when it wants
a decoded object rather than raw bytes.

```rust
#[derive(Debug, Serialize, Deserialize)]
pub struct AssetManager { /* private */ }

impl AssetManager {
    pub fn new(storage: Arc<dyn Storage + Send + Sync>, storage_settings: storage::Settings,
               num_snapshot_nodes: u64, num_chunk_refs: u64, num_transaction_changes: u64,
               num_bytes_attributes: u64, num_bytes_chunks: u64, compression_level: u8) -> Self;
    pub fn new_no_cache(storage: Arc<dyn Storage + Send + Sync>,
                        storage_settings: storage::Settings, compression_level: u8) -> Self;
    pub fn new_with_config(storage: Arc<dyn Storage + Send + Sync>,
                           storage_settings: storage::Settings,
                           config: &icechunk::config::CachingConfig, compression_level: u8) -> Self;

    pub async fn write_manifest(&self, manifest: Arc<Manifest>) -> RepositoryResult<u64>;
    pub async fn fetch_manifest(&self, id: &ManifestId, size: u64) -> RepositoryResult<Arc<Manifest>>;
    pub async fn fetch_manifest_unknown_size(&self, id: &ManifestId) -> RepositoryResult<Arc<Manifest>>;
    pub async fn write_snapshot(&self, snapshot: Arc<Snapshot>) -> RepositoryResult<()>;
    pub async fn fetch_snapshot(&self, id: &SnapshotId) -> RepositoryResult<Arc<Snapshot>>;
    pub async fn write_transaction_log(&self, id: SnapshotId, log: Arc<TransactionLog>) -> RepositoryResult<()>;
    pub async fn fetch_transaction_log(&self, id: &SnapshotId) -> RepositoryResult<Arc<TransactionLog>>;
    pub async fn write_chunk(&self, id: ChunkId, bytes: Bytes) -> RepositoryResult<()>;
    pub async fn fetch_chunk(&self, id: &ChunkId, range: &Range<ChunkOffset>) -> RepositoryResult<Bytes>;
    pub async fn snapshot_ancestry(self: Arc<Self>, id: &SnapshotId)
        -> RepositoryResult<impl Stream<Item = RepositoryResult<SnapshotInfo>>>;
    pub async fn get_snapshot_last_modified(&self, id: &SnapshotId) -> RepositoryResult<DateTime<Utc>>;
}

#[derive(Debug, Clone)]
pub struct FileWeighter;
```

`AssetManager` is `Serialize + Deserialize` (the caches are skipped), which is
what keeps `Session` and `Repository` serializable. There is **no public
`clear()` or `invalidate()`**: a fresh view is a new `AssetManager` or a
re-`open`ed `Repository`.

### 9.4 `icechunk::config::RepositoryConfig` — every field optional

```rust
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq, Default)]
pub struct RepositoryConfig {
    pub inline_chunk_threshold_bytes: Option<u16>,
    pub unsafe_overwrite_refs: Option<bool>,
    pub get_partial_values_concurrency: Option<u16>,
    pub compression: Option<CompressionConfig>,
    pub caching: Option<CachingConfig>,
    pub storage: Option<icechunk::storage::Settings>,
    pub virtual_chunk_containers: Option<HashMap<ContainerName, VirtualChunkContainer>>,
    pub manifest: Option<ManifestConfig>,
}

impl RepositoryConfig {
    pub fn inline_chunk_threshold_bytes(&self) -> u16;   // 512
    pub fn unsafe_overwrite_refs(&self) -> bool;         // false
    pub fn get_partial_values_concurrency(&self) -> u16; // 10
    pub fn compression(&self) -> &CompressionConfig;
    pub fn caching(&self) -> &CachingConfig;
    pub fn storage(&self) -> Option<&icechunk::storage::Settings>;
    pub fn manifest(&self) -> &ManifestConfig;
    pub fn merge(&self, other: Self) -> Self;            // `other` wins, field by field
    pub fn set_virtual_chunk_container(&mut self, cont: VirtualChunkContainer);
    pub fn get_virtual_chunk_container(&self, name: &str) -> Option<&VirtualChunkContainer>;
    pub fn virtual_chunk_containers(&self) -> impl Iterator<Item = &VirtualChunkContainer>;
    pub fn clear_virtual_chunk_containers(&mut self);
}

#[derive(Debug, PartialEq, Eq, Default, Serialize, Deserialize, Clone, Copy)]
#[serde(rename_all = "snake_case")]
pub enum CompressionAlgorithm { #[default] Zstd }

// Each of the four structs below derives exactly this, `Default` included, so
// a caller may write `CachingConfig { num_chunk_refs: Some(100), ..Default::default() }`:
#[derive(Debug, PartialEq, Eq, Serialize, Deserialize, Clone, Default)]

pub struct CompressionConfig { pub algorithm: Option<CompressionAlgorithm>, pub level: Option<u8> }
impl CompressionConfig { pub fn algorithm(&self) -> CompressionAlgorithm;
                         pub fn level(&self) -> u8;            // 1
                         pub fn merge(&self, other: Self) -> Self; }

pub struct CachingConfig {
    pub num_snapshot_nodes: Option<u64>,       // 10_000
    pub num_chunk_refs: Option<u64>,           // 5_000_000
    pub num_transaction_changes: Option<u64>,  // 0
    pub num_bytes_attributes: Option<u64>,     // 0
    pub num_bytes_chunks: Option<u64>,         // 0
}
impl CachingConfig { /* one getter per field, returning the default above, plus merge */ }

pub struct ManifestPreloadConfig { pub max_total_refs: Option<u32>, pub preload_if: Option<ManifestPreloadCondition> }
impl ManifestPreloadConfig { pub fn max_total_refs(&self) -> u32;   // 10_000
                             pub fn preload_if(&self) -> &ManifestPreloadCondition;
                             pub fn merge(&self, other: Self) -> Self; }

pub struct ManifestConfig { pub preload: Option<ManifestPreloadConfig> }
impl ManifestConfig { pub fn preload(&self) -> &ManifestPreloadConfig;
                      pub fn merge(&self, other: Self) -> Self; }

#[derive(Debug, PartialEq, Eq, Serialize, Deserialize, Clone)]
#[serde(rename_all = "snake_case")]
pub enum ManifestPreloadCondition {
    Or(Vec<ManifestPreloadCondition>), And(Vec<ManifestPreloadCondition>),
    PathMatches { regex: String }, NameMatches { regex: String },
    NumRefs { from: std::ops::Bound<u32>, to: std::ops::Bound<u32> },
    True, False,
}
```

Merge semantics the verifier asserts:

- `Repository::create(None, ..)` **persists no configuration file**;
  `Repository::fetch_config` returns `None` and `repo.config()` equals
  `RepositoryConfig::default()`. `save_config()` writes one, and what
  `Repository::fetch_config` then returns equals what was saved. The `ETag`
  `save_config` returns is not inspected (section 12).
- `Repository::open(Some(cfg), ..)` merges `cfg` over the persisted
  configuration: a field explicitly set in `cfg` wins, every unset field falls
  back to the persisted value and then to the built-in default above.
- The persisted document keeps `None` for fields nobody set — persisting a
  config with only `inline_chunk_threshold_bytes` set leaves `caching` as `None`
  on the way back out.
- `reopen(Some(cfg), None)` merges per **nested** field: overriding
  `caching.num_chunk_refs` leaves `inline_chunk_threshold_bytes` and
  `caching.num_snapshot_nodes` as they were. `reopen(None, None)` changes
  nothing.

Because every field is now `Option`, `config == RepositoryConfig::default()` no
longer means "all built-in defaults materialised". Compare through the getters.

### 9.5 `icechunk::storage`

```rust
// both derive #[derive(Debug, PartialEq, Eq, Serialize, Deserialize, Clone, Default)]
pub struct ConcurrencySettings {
    pub max_concurrent_requests_for_object: Option<NonZeroU16>,  // 18
    pub ideal_concurrent_request_size: Option<NonZeroU64>,       // 12 * 1024 * 1024
}
impl ConcurrencySettings {
    pub fn max_concurrent_requests_for_object(&self) -> NonZeroU16;  // 18
    pub fn ideal_concurrent_request_size(&self) -> NonZeroU64;       // 12 * 1024 * 1024
    pub fn merge(&self, other: Self) -> Self;
}

pub struct Settings { pub concurrency: Option<ConcurrencySettings> }
impl Settings { pub fn concurrency(&self) -> &ConcurrencySettings; pub fn merge(&self, other: Self) -> Self; }

pub enum Reader { Asynchronous(Box<dyn AsyncRead + Unpin + Send>), Synchronous(Box<dyn Buf + Unpin + Send>) }
impl Reader { pub async fn to_bytes(self, expected_size: usize) -> StorageResult<Bytes>;
              pub fn into_read(self) -> Box<dyn Read + Unpin + Send>; }

pub fn split_in_multiple_requests(range: &Range<u64>, ideal_req_size: u64, max_requests: u16)
    -> impl Iterator<Item = Range<u64>>;
```

The `// 18` and `// 12 * 1024 * 1024` above are the **struct** fall-backs — what
`ConcurrencySettings`' getters return when the fields are `None`. They are not a
promise about any particular backend: `default_settings()` is implemented per
backend and may report concurrency of its own (the in-memory and local
filesystem stores do). What is required of a repository is only that
`storage_settings()` agrees with the backend's `default_settings()` when the
configuration sets none.

**Every `Storage` trait method now takes `settings: &Settings` as its first
argument**, and the trait gained:

```rust
fn default_settings(&self) -> Settings;
async fn fetch_manifest_known_size(&self, settings: &Settings, id: &ManifestId, size: u64) -> StorageResult<Reader>;
async fn fetch_manifest_unknown_size(&self, settings: &Settings, id: &ManifestId)
    -> StorageResult<Box<dyn AsyncRead + Unpin + Send>>;
async fn get_snapshot_last_modified(&self, settings: &Settings, snapshot: &SnapshotId) -> StorageResult<DateTime<Utc>>;
async fn root_is_clean(&self) -> StorageResult<bool>;
async fn delete_objects(&self, settings: &Settings, prefix: &str, ids: BoxStream<'_, String>) -> StorageResult<usize>;
async fn list_objects<'a>(&'a self, settings: &Settings, prefix: &str)
    -> StorageResult<BoxStream<'a, StorageResult<ListInfo<String>>>>;
```

The verifier calls exactly these, on `&(dyn Storage + Send + Sync)`:

```rust
fn default_settings(&self) -> Settings;
async fn fetch_snapshot(&self, settings: &Settings, id: &SnapshotId)
    -> StorageResult<Box<dyn AsyncRead + Unpin + Send>>;
async fn fetch_manifest_unknown_size(&self, settings: &Settings, id: &ManifestId)
    -> StorageResult<Box<dyn AsyncRead + Unpin + Send>>;
async fn fetch_transaction_log(&self, settings: &Settings, id: &SnapshotId)
    -> StorageResult<Box<dyn AsyncRead + Unpin + Send>>;
async fn fetch_chunk(&self, settings: &Settings, id: &ChunkId, range: &Range<ChunkOffset>)
    -> StorageResult<Bytes>;
async fn fetch_config(&self, settings: &Settings) -> StorageResult<Option<(Bytes, ETag)>>;
async fn get_ref(&self, settings: &Settings, ref_key: &str) -> StorageResult<Bytes>;
// raw write, used to put a deliberately doctored metadata object in the store (section 5.4)
async fn write_snapshot(&self, settings: &Settings, id: SnapshotId,
                        metadata: Vec<(String, String)>, bytes: Bytes) -> StorageResult<()>;
// provided methods; each yields ListInfo<Id> values with a public `id` field
async fn list_snapshots(&self, settings: &Settings)        -> StorageResult<BoxStream<StorageResult<ListInfo<SnapshotId>>>>;
async fn list_manifests(&self, settings: &Settings)        -> StorageResult<BoxStream<StorageResult<ListInfo<ManifestId>>>>;
async fn list_chunks(&self, settings: &Settings)           -> StorageResult<BoxStream<StorageResult<ListInfo<ChunkId>>>>;
async fn list_transaction_logs(&self, settings: &Settings) -> StorageResult<BoxStream<StorageResult<ListInfo<SnapshotId>>>>;
```

`get_ref`'s `ref_key` is the key **relative to the reference prefix**, not the
full object key: the tip of `main` at sequence 0 is
`get_ref(&settings, "branch.main/ZZZZZZZZ.json")` and a tag is
`get_ref(&settings, "tag.$NAME/ref.json")`. `fetch_chunk`'s range is a byte
range within the chunk object; `0..length` returns the whole of a chunk that was
written with that length.

`write_snapshot` / `write_manifest` / `write_transaction_log` now take
`(settings, id, metadata: Vec<(String,String)>, bytes: Bytes)` — raw bytes, not
a typed value. The typed path is `AssetManager`. Writing at an id that already
exists **replaces** the object rather than failing or writing a second one,
which is what lets `expire` rewrite a snapshot in place under its own id
(section 6.1) and what the doctored-header case of section 5.4 relies on.

`ObjectStorage` gained `pub fn from_config(ObjectStorageConfig)`,
`pub fn artificially_sort_refs_in_mem(&self) -> bool`,
`pub fn supports_metadata(&self) -> bool` and
`pub async fn all_keys(&self) -> StorageResult<Vec<String>>` (every key in the
store, as a full object key such as `refs/tag.v1/ref.json` — for tests).
`ObjectStorage::new_in_memory()` and
`ObjectStorage::new_local_filesystem(&std::path::Path)` are unchanged. Both
return `StorageResult<ObjectStorage>` — **the value, not an `Arc`** — so the
verifier wraps one itself (`Arc::new(ObjectStorage::new_in_memory()?)`) and
passes a clone of that `Arc` where an `Arc<dyn Storage + Send + Sync>` is
wanted, keeping the concrete handle for `all_keys()`. The crate-root re-exports
`new_in_memory_storage()` and `new_local_filesystem_storage(&std::path::Path)`
are also unchanged, and both return `StorageResult<Arc<dyn Storage>>`.

The `Storage` trait is **sealed** (`private::Sealed`), so no external crate can
implement it. The verifier only uses the two backends above.

### 9.6 Inherited types the drivers construct or destructure

These carry over from milestones 1 and 2 unchanged. They are listed because the
verifier names them directly, so renaming or reshaping one breaks the drivers.

```rust
// icechunk::format
pub struct Path(/* private */);          // Path::root(), TryFrom<&str> from "/a/b"
pub struct ChunkIndices(pub Vec<u32>);
pub type SnapshotId = ObjectId<12, SnapshotTag>;   // Display -> 20-char Crockford
pub type ManifestId = ObjectId<12, ManifestTag>;
pub type ChunkId    = ObjectId<12, ChunkTag>;
pub type ChunkOffset = u64;
pub type ChunkLength = u64;

pub enum ByteRange { Bounded(Range<ChunkOffset>), From(ChunkOffset),
                     Last(ChunkLength), Until(ChunkOffset) }
impl ByteRange { pub const ALL: Self; /* = From(0) */ }

// icechunk::format::manifest
pub struct ChunkRef { pub id: ChunkId, pub offset: ChunkOffset, pub length: ChunkLength }
pub struct VirtualChunkRef { pub location: VirtualChunkLocation, pub offset: ChunkOffset,
                             pub length: ChunkLength, pub checksum: Option<Checksum> }
pub struct VirtualChunkLocation(pub String);
impl VirtualChunkLocation {
    pub fn from_absolute_path(path: &str) -> Result<VirtualChunkLocation, VirtualReferenceError>;
}

// icechunk::format::snapshot
pub struct NodeSnapshot { pub id: NodeId, pub path: Path,
                          pub user_attributes: Option<UserAttributesSnapshot>,
                          pub node_data: NodeData }
pub enum NodeData { Array(ZarrArrayMetadata, Vec<ManifestRef>), Group }
pub struct ZarrArrayMetadata {
    pub shape: ArrayShape,                       // = Vec<u64>
    pub data_type: DataType,
    pub chunk_shape: ChunkShape,
    pub chunk_key_encoding: ChunkKeyEncoding,
    pub fill_value: FillValue,
    pub codecs: Vec<Codec>,
    pub storage_transformers: Option<Vec<StorageTransformer>>,
    pub dimension_names: Option<DimensionNames>,
}

// icechunk::metadata
pub struct ChunkShape(pub Vec<std::num::NonZeroU64>);
pub enum ChunkKeyEncoding { Slash, Dot, Default }
pub enum DataType { /* Int32 among others */ }
pub enum FillValue { /* Int32(i32) among others */ }

// icechunk::config
pub enum Credentials { S3(S3Credentials), Gcs(GcsCredentials), Azure(AzureCredentials) }
pub enum ObjectStoreConfig { InMemory, LocalFileSystem(PathBuf), S3(..), S3Compatible(..),
                             Gcs(..), Azure(..), Tigris(..) }  // re-exported at the crate root
```

And these `Session` methods, unchanged in shape since milestone 2:

```rust
pub async fn add_group(&mut self, path: Path) -> SessionResult<()>;
pub async fn add_array(&mut self, path: Path, metadata: ZarrArrayMetadata) -> SessionResult<()>;
pub async fn update_array(&mut self, path: Path, metadata: ZarrArrayMetadata) -> SessionResult<()>;
pub async fn set_chunk_ref(&mut self, path: Path, coord: ChunkIndices,
                           data: Option<ChunkPayload>) -> SessionResult<()>;
pub async fn get_node(&self, path: &Path) -> SessionResult<NodeSnapshot>;
pub async fn get_array(&self, path: &Path) -> SessionResult<NodeSnapshot>;
pub async fn get_group(&self, path: &Path) -> SessionResult<NodeSnapshot>;
pub async fn get_chunk_ref(&self, path: &Path, coords: &ChunkIndices)
    -> SessionResult<Option<ChunkPayload>>;
pub async fn get_chunk_reader(&self, path: &Path, coords: &ChunkIndices, byte_range: &ByteRange)
    -> SessionResult<Option<Pin<Box<dyn Future<Output = SessionResult<Bytes>> + Send>>>>;
pub fn get_chunk_writer(&self)
    -> impl FnOnce(Bytes) -> Pin<Box<dyn Future<Output = SessionResult<ChunkPayload>> + Send>>;
pub async fn all_virtual_chunk_locations(&self)
    -> SessionResult<impl Stream<Item = SessionResult<String>> + '_>;
pub async fn rebase(&mut self, solver: &dyn ConflictSolver) -> SessionResult<()>;
pub async fn merge(&mut self, changes: ChangeSet) -> SessionResult<()>;
pub fn discard_changes(&mut self) -> ChangeSet;

// icechunk::session
pub async fn get_chunk(reader: Option<Pin<Box<dyn Future<Output = SessionResult<Bytes>> + Send>>>)
    -> SessionResult<Option<Bytes>>;
```

## 10. The inherited contract, in this release's spelling

These milestones are still required and are re-exercised here.

### 10.1 Milestone 1 — the spec-1 repository format

Normative source: `icechunk-spec/spec.md` → "### File Layout", "### Reference
Files", "#### Creating and Updating Branches", "#### Tags" and "### Chunk
Manifest Files". `PROFILE.md` §D4 adds the two prefixes `spec.md` does not list.

- Objects live under fixed prefixes of the storage root: `refs/`, `snapshots/`,
  `manifests/`, `chunks/` — and, not listed by `spec.md` but required,
  `transactions/` for transaction logs, plus `config.yaml` at the root.
- A branch's reference files are `refs/branch.$NAME/$ENCODED_SEQUENCE.json`. The
  sequence number is encoded by subtracting it from `1_099_511_627_775`,
  Base32-Crockford encoding the result over five bytes, giving eight characters,
  so **the newest sequence sorts first**: sequence 0 of `main` is
  `refs/branch.main/ZZZZZZZZ.json` and sequence 1 is
  `refs/branch.main/ZZZZZZZY.json`. The default branch name is `main`.
- A tag's reference file is `refs/tag.$NAME/ref.json`.
- A reference file is a JSON object with **exactly one member**, `"snapshot"`,
  whose value is the Base32-Crockford spelling of the 12-byte snapshot id — a
  20-character string, the same text `SnapshotId`'s `Display` produces.
- A chunk reference is exactly one of three kinds
  (`format::manifest::ChunkPayload`, `#[non_exhaustive]`):
  `Inline(Bytes)` for a small chunk stored in the manifest itself,
  `Ref(ChunkRef { id, offset, length })` for a chunk materialised as its own
  object, and `Virtual(VirtualChunkRef { location, offset, length, checksum })`
  for a range of a foreign file. `Session::get_chunk_writer()` chooses between
  the first two using `inline_chunk_threshold_bytes` (`> threshold` ⇒
  materialise), and all three read back through
  `Session::get_chunk_reader(&Path, &ChunkIndices, &ByteRange)` fed to
  `icechunk::session::get_chunk`. **One materialised chunk is one object.**
  `spec.md` → "### Chunk Files" permits a chunk file to hold several chunks, but
  nothing in this task produces or requires such packing, and the
  garbage-collection groups count chunk objects on the assumption that a
  materialised `set_chunk_ref` wrote exactly one.
- A repository is opened at a branch tip, a tag or a snapshot id, and all three
  reach the same data when they resolve to the same snapshot.

### 10.2 Milestone 2 — sessions, conflicts, rebase, virtual containers

Normative sources: `icechunk-spec/design/002-rebase-commit-improvements.md`,
`icechunk-spec/design/001-virtual-chunk-improvements.md`,
`icechunk-spec/guide/icechunk-python-quickstart.md`.

- `Repository`, `Session` and `Store` are separate entities.
  `writable_session(branch)` is anchored at that branch's tip and reports
  `branch() == Some(branch)` and `read_only() == false`.
- **A writable session commits once.** A successful `commit` resets the change
  set, moves `snapshot_id()` to the new snapshot, and makes the session
  read-only (`branch()` becomes `None`). A second `commit` through it fails with
  `SessionError::ReadOnlySession`, and so does `rebase`. A read-only session
  behaves the same way: it can stage changes, but it can never publish them.
- `readonly_session(&VersionInfo)` opens at a branch tip, a tag or a snapshot id
  and sees exactly that version.
- A stale commit fails with
  `SessionError::Conflict { expected_parent, actual_parent }`, where
  `actual_parent` is the branch tip that won and `expected_parent` is the
  session's own parent.
- `rebase(&dyn ConflictSolver)` fast-forwards the intervening commits, oldest
  first. `icechunk::conflicts::detector::ConflictDetector` — a **unit struct**,
  written `ConflictDetector` with no arguments — never patches and
  always fails when a conflict is present;
  `icechunk::conflicts::basic_solver::BasicConflictSolver` (with
  `VersionSelection` options and `Default`) resolves what its policy allows.
- A rebase that cannot proceed fails with
  `SessionError::RebaseFailed { snapshot, conflicts }`, where `snapshot` is the
  commit the conflict arose at and `conflicts` is a `Vec<icechunk::conflicts::Conflict>`.
  The classification is a fixed enumeration; the verifier matches
  `Conflict::NewNodeConflictsWithExistingNode(Path)` and
  `Conflict::ZarrMetadataDoubleUpdate(Path)` by variant and path.
- After a successful rebase the commit succeeds and the result contains both
  writers' changes.
- Virtual chunk containers are configured through
  `RepositoryConfig::virtual_chunk_containers`, keyed by
  `ContainerName` (`= String`), with
  `icechunk::virtual_chunks::VirtualChunkContainer { name: ContainerName, url_prefix: String, store: ObjectStoreConfig }`
  — that module path is where the type lives, and its three fields are public,
  so the verifier builds one with a struct literal.
  A repository created or opened with an explicit `virtual_chunk_containers` map
  uses **exactly** that map: it replaces the built-in default set rather than
  adding to it, so a name the caller did not supply is absent from
  `get_virtual_chunk_container` and from `virtual_chunk_containers()`. A
  repository that supplies none keeps the built-in defaults.
  A location is matched to its container by URL prefix through
  `Session::matching_container(&VirtualChunkLocation) -> Option<&VirtualChunkContainer>`,
  which returns `None` when no configured container handles the URL.
  `VirtualChunkLocation::from_absolute_path(&str)` builds a location;
  `Session::all_virtual_chunk_locations()` lists the ones a session can see.
  `ObjectStoreConfig::LocalFileSystem(PathBuf)` with `url_prefix = "file://"` is
  the offline container the verifier uses.

## 11. Migration from milestone 2

Everything below was public at `0.1.0-alpha.8` and is gone, moved or
differently shaped at `0.1.0`. A milestone-2 implementation that keeps any of
the left-hand forms will not compile against the verifier's drivers.

### 11.1 Removed outright

| Removed | Replacement |
| --- | --- |
| `icechunk::MemCachingStorage` (crate-root re-export) | none — use `icechunk::asset_manager::AssetManager` |
| `icechunk::storage::caching` module and `storage::caching::MemCachingStorage` | `AssetManager::new_with_config(storage, settings, &CachingConfig, level)`, or `new_no_cache` / `new` |
| `Repository::add_in_mem_asset_caching(storage) -> Arc<dyn Storage + Send + Sync>` | delete the call; pass the raw `Arc<dyn Storage + Send + Sync>` to `Repository::create` / `open` |
| `RepositoryError::AlreadyInitialized` | `RepositoryError::ParentDirectoryNotClean` — **and the trigger changed**: alpha.8 checked "the default branch already exists", `0.1.0` checks "the prefix is non-empty" |
| `icechunk::format::IcechunkFormatVersion` (`= u16`) | `format::format_constants::SpecVersionBin` |
| `format_constants::LATEST_ICECHUNK_{MANIFEST,SNAPSHOT,TRANSACTION_LOG}_FORMAT` and their `_CONTENT_TYPE` / `_VERSION_METADATA_KEY` siblings | `SpecVersionBin::current()` plus `ICECHUNK_FILE_TYPE_{MANIFEST,SNAPSHOT,TRANSACTION_LOG}` and `LATEST_ICECHUNK_FORMAT_VERSION_METADATA_KEY` |
| `format::snapshot::SnapshotMetadata { id, written_at, message }` | `format::snapshot::SnapshotInfo { id, parent_id, flushed_at, message, metadata }` — **`written_at` is renamed `flushed_at`**, and two fields are added |
| `Snapshot::empty()` | `Snapshot::initial()` |
| `Snapshot::local_ancestry(self: Arc<Self>) -> impl Iterator<Item = SnapshotMetadata>` | `Repository::snapshot_ancestry` / `AssetManager::snapshot_ancestry` — a `Stream`, not an `Iterator` |
| every public `Snapshot` field (`metadata`, `properties`, `manifest_files`, `attribute_files`, `total_parents`, `short_term_parents`, `short_term_history`, `started_at`, `icechunk_snapshot_format_version`, `icechunk_snapshot_format_flags`) | private; use the accessors in section 9.2 |
| `format::manifest::ManifestExtents(pub Vec<ChunkIndices>)` — the public tuple struct | a **private** alias `type ManifestExtents = Range<ChunkIndices>`; read `ManifestRef.extents` as a `Range<ChunkIndices>` and use `.start` / `.end` |
| `Manifest::chunks(&self) -> &BTreeMap<(NodeId, ChunkIndices), ChunkPayload>` | `Manifest::chunk_payloads()` |
| `impl FromIterator<ChunkInfo> for Manifest` | `Manifest::from_iter(iter).await? -> Option<Manifest>` |
| `ManifestFileInfo.format_version`, `AttributeFileInfo.format_version` | `ManifestFileInfo { id, size_bytes, num_rows }`; `AttributeFileInfo { id }` |
| `StorageError::MsgPackDecodeError`, `StorageError::MsgPackEncodeError` | `RepositoryError::{SerializationError, DeserializationError}` |

Nested-module changes: `format::serializers` (with `format::serializers::current`)
is **added**; `storage::caching` is **removed**. At the crate root,
`asset_manager` is added and the `MemCachingStorage` re-export is dropped; every
other root re-export (`ObjectStoreConfig`, `RepositoryConfig`, `Repository`,
`new_in_memory_storage`, `new_local_filesystem_storage`, `new_s3_storage`,
`ObjectStorage`, `Storage`, `StorageError`, `Store`) is unchanged.

### 11.2 Signature and return-type changes

| Item | alpha.8 | 0.1.0 |
| --- | --- | --- |
| `Repository::ancestry` | `(&self, snapshot_id: &SnapshotId) -> …Stream<SnapshotMetadata>` | `(&self, version: &VersionInfo) -> …Stream<SnapshotInfo> + '_` — **argument kind and item type both changed**; the by-id form is now `snapshot_ancestry(&SnapshotId)` |
| `Repository::list_branches` / `list_tags` | `HashSet<String>` | `BTreeSet<String>` |
| `Repository::delete_branch("main")` | succeeded | `Err(RepositoryError::CannotDeleteMain)` |
| `Repository::create` | `AlreadyInitialized` when the default branch existed | `ParentDirectoryNotClean` when `storage.root_is_clean()` is false; also merges the passed config over `RepositoryConfig::default()` |
| `Repository::save_config` | used a non-optional `ETag` | `config_etag` is now `Option<ETag>`; saving a never-persisted config passes `None` |
| `Session::create_readonly_session` | `(config, storage, virtual_resolver, snapshot_id)` | `(config, storage_settings, storage, asset_manager, virtual_resolver, snapshot_id)` |
| `Session::create_writable_session` | `(config, storage, virtual_resolver, branch_name, snapshot_id)` | `(config, storage_settings, storage, asset_manager, virtual_resolver, branch_name, snapshot_id)` |
| `ops::gc::GCConfig::new` | 5 args | 6 args — `dangling_snapshots: Action` added last |
| `ops::gc::GCConfig::clean_all` | 3 args, filling four categories | same 3 args, but it now also fills `dangling_snapshots` |
| `ops::gc::garbage_collect` | `(storage, config)` | `(storage, storage_settings, asset_manager, config)` |
| `refs::create_tag` / `update_branch` / `fetch_tag` / `fetch_branch_tip` / `fetch_ref` / `delete_branch` / `list_refs` / `list_tags` / `list_branches`, and `Ref::fetch` | no settings argument | all take `storage_settings: &storage::Settings` right after `storage` |
| `refs::list_refs` / `list_tags` / `list_branches` | `HashSet` | `BTreeSet`, and `list_refs` / `list_tags` now **hide deleted tags** |
| **every `Storage` trait method** | `(&self, …)` | `(&self, settings: &Settings, …)` — obtain one from `storage.default_settings()` or `repo.storage_settings()` and thread it |
| `Storage::write_snapshot` / `write_manifest` / `write_transaction_log` | `(id, Arc<X>)` | `(settings, id, metadata: Vec<(String,String)>, bytes: Bytes)` |
| `Storage::fetch_snapshot` / `fetch_manifest` / `fetch_transaction_log` | returned `Arc<X>` | return `Box<dyn AsyncRead + Unpin + Send>` (or `Reader`); the typed path is `AssetManager::fetch_*` |
| `Manifest::new` | `BTreeMap<(NodeId, ChunkIndices), ChunkPayload>` | `BTreeMap<NodeId, BTreeMap<ChunkIndices, ChunkPayload>>` |
| `Manifest::from_stream` | `Result<Self, E>` | `Result<Option<Self>, E>` — an empty stream writes no manifest |
| `RepositoryConfig` fields | plain values | all `Option<..>`, read through the same-named getters (section 9.4) |
| `ByteRange::from((None, Some(end)))` | `Bounded(0..end)` | `Until(end)` — a `match` over `ByteRange` now needs an `Until` arm |
| `ObjectId<SIZE, T>` serde form | a Crockford-32 **string** | a **byte array**, via `#[serde(with = "serde_bytes")]`. `Display`, `String::from` and `TryFrom<&str>` still use the Crockford string, so reference files are unchanged, but any msgpack or JSON persisted by milestone 2 is unreadable. |
| `S3Options` | `{ region, endpoint_url, allow_http }` | `+ anonymous: bool` |
| `Path` deserialization | plain | `#[serde_as(as = "TryFromInto<String>")]`, so a `Path` is a string on the wire; `Path` also gained `pub fn name(&self) -> Option<&str>` |
| `VirtualChunkResolver::new` / `VirtualChunkContainer::new` | see alpha.8 | multi-argument; construct `VirtualChunkContainer` from its public fields |

### 11.3 Behavioural changes with no signature change

- `Repository::create` now fails on **any** non-empty prefix. A milestone-2 test
  that created two repositories under one directory, or created one under a
  directory it had already written to, now fails.
- `commit` can now fail with `SessionError::InvalidSnapshotTimestampOrdering`
  (a child timestamp not strictly greater than its parent's) or
  `SessionError::InvalidSnapshotTimestamp` (object-store clock skew).
- Snapshots, manifests and transaction logs are Zstd-compressed behind the
  39-byte header. Any milestone-2 assertion on raw object bytes, object size, or
  "is this MessagePack" is invalid.
- Manifests are **one per array**: a commit touching *N* arrays writes *N*
  manifest objects.
- `list_tags` and `list_refs` hide deleted tags and return sorted `BTreeSet`s.
- `delete_branch("main")` is refused.

### 11.4 Names a milestone-2 implementation will hit mechanically

`SnapshotMetadata` → `SnapshotInfo`; `.written_at` → `.flushed_at`;
`Snapshot::empty()` → `Snapshot::initial()`;
`Repository::ancestry(&snapshot_id)` → `Repository::snapshot_ancestry(&snapshot_id)`
or `Repository::ancestry(&VersionInfo::SnapshotId(id))`;
`IcechunkFormatVersion` → `SpecVersionBin`;
`MemCachingStorage::new(..)` → `AssetManager::new(..)` / `new_with_config(..)` /
`new_no_cache(..)`.

> Note for readers of the Python guide: `icechunk-spec/guide/icechunk-python-version-control.md`
> prints `SnapshotInfo(id=…, parent_id=…, written_at=…, message=…)`. That is the
> Python binding's field name. **The Rust field is `flushed_at`**, and that is
> what this milestone requires.

## 12. Out of scope at this milestone

Stated so you can spend the budget on what is measured.

- **Internal organisation.** Module layout inside `icechunk/src`, private type
  and function names, file boundaries, where a helper lives.
- **Error message wording.** The verifier matches error *variants* where this
  file names one (`ParentDirectoryNotClean`, `TagAlreadyExists`, `RefNotFound`,
  `ReadOnlySession`, `Conflict`, `RebaseFailed`, `NotAnArray`,
  `CannotDeleteMain`) and otherwise only that the operation failed. No
  `Display` text, no `source()` chain and no log output is asserted.
- **`SessionError::InvalidSnapshotTimestamp`, the 600-second clock-drift
  limit.** It compares a snapshot's timestamp against
  `Storage::get_snapshot_last_modified`, which for the in-memory and local
  filesystem backends is the same machine clock, and `Storage` is sealed so no
  external crate can supply a skewed one. **It is therefore out of scope and
  untested.** Only the ordering rule,
  `SessionError::InvalidSnapshotTimestampOrdering`, is observable, and only as
  the requirement that `flushed_at` strictly increases from parent to child
  (section 8.2).
- **Object-store metadata on metadata files** (section 5.3). It is written but
  never read back by this release, and the two offline backends the verifier
  uses do not both support it. Not asserted.
- **Chunk-file headers.** `FileTypeBin::Chunk` exists; chunk objects carry no
  header, so its discriminant is never read off an object. The one thing
  section 5 does require of it is negative: no variant of `FileTypeBin` may take
  the value `9`, because `FileTypeBin::try_from(9)` must be an error.
- **Attribute files.** Not produced at this release; `attributes_deleted` is
  asserted to be `0` and nothing else about attributes is in scope.
- **Cloud and emulator backends.** S3, Tigris, GCS and Azure constructors,
  `S3Credentials`, `GcsCredentials`, `AzureCredentials`, `CredentialsFetcher`,
  and the parallel byte-range manifest fetch they make interesting. No group
  needs network, credentials, MinIO or Azurite.
- **Python bindings.** `icechunk-python` is not built, and neither is the
  `allow_pickling` opt-in, `to_icechunk`, or anything else that exists only
  there. The Rust `Session::as_bytes` / `from_bytes` round trip is public but is
  out of scope at this milestone.
- **Manifest preloading and caching behaviour.** `ManifestPreloadCondition` and
  `CachingConfig` must exist with the getters and defaults in section 9.4,
  because configuration merge is in scope; whether a manifest is actually
  preloaded, and what the caches hold, are performance properties and are not
  observed.
- **The persisted configuration's serialized spelling.** `config.yaml` must be
  plain text with no binary header and must round-trip through
  `Repository::fetch_config`, but its key names, YAML dialect and member order
  are not asserted. Neither is the value of any `ETag`: the verifier checks that
  `save_config` succeeds and that the configuration comes back, not what the
  tag string contains.
- **Performance, allocation, concurrency strategy, and iteration order** where
  this file does not fix one. `chunk_coordinates` and the `list_*` streams are
  compared as sets.
- **Your own tests.** The verifier compiles its own drivers and never runs
  yours. `cargo test` in `/app` is not part of the requirement.
