# TEST_INTERFACE.md — milestone 07, the asynchronous API release

This is the interface contract for this milestone. It is self-contained: every
behaviour the verifier observes is stated here or in `/app/icechunk-spec/`.
Where this document and the specification corpus disagree, `PROFILE.md` in that
corpus records which one wins.

The crate is `icechunk`, version **`0.3.4`**, Rust **edition 2024**, at
`/app/icechunk/`. The workspace at `/app/Cargo.toml` has
`default-members = ["icechunk"]`, so a bare `cargo build` builds the library
crate alone. Two non-default features exist: `logs` (gating
`icechunk::initialize_tracing`) and `cli` (gating the `icechunk` binary at
`src/bin/icechunk/main.rs`, `required-features = ["cli"]`).

---

## 1. What this milestone adds

### 1.1 A virtual chunk container's `url_prefix` must end in `/`

`icechunk::virtual_chunks::VirtualChunkContainer::new` gains a **first** check,
ahead of URL parsing and ahead of every scheme/store check that already existed:

```rust
pub fn new(url_prefix: String, store: ObjectStoreConfig) -> Result<Self, String>;
```

* If `url_prefix` does not end in `'/'`, it returns `Err` and the message
  contains the text **`must end in a / character`**. No normalisation, no
  warning — a hard constructor error.
* Because the check runs first, a string that is both unseparated and
  unparseable as a URL is reported as unseparated.
* A separated prefix is accepted and `url_prefix()` returns it unchanged.
* Every validation that existed before still applies to a separated prefix: an
  `s3://`, `gcs://`, `az://`, `tigris://` or `http(s)://` prefix must carry a
  host; a `file://` prefix must carry a host **or** a path longer than `/`, so a
  bare `file:///` is refused, and no path segment may be `.` or `..`; and the
  prefix scheme must match the `ObjectStoreConfig` variant.
* `url::Url::parse` collapses `.` and `..` segments for `file:` URLs before any
  of those checks sees them, so `file:///data/../` arrives as `file:///` and is
  refused as a path no longer than `/` rather than as a non-canonical one. Write
  the canonical-path check anyway — it is in the release — but it is
  unobservable through this constructor and §5 records that no group asserts
  it.

The error type is a plain `String`; there is no error enum to match on.

### 1.2 Prefix matching is exact, so a sibling prefix is not covered

Container matching is a raw `str::starts_with` against `url_prefix()`, longest
prefix first. Requiring the separator is therefore a security property: an
authorisation for `s3://foo` used to cover `s3://foo-production` as well.

`icechunk::virtual_chunks::VirtualChunkResolver::new` — whose signature is
unchanged —

```rust
pub fn new(
    containers: impl Iterator<Item = VirtualChunkContainer>,
    credentials: HashMap<String, Option<Credentials>>,
    settings: storage::Settings,
) -> Self;
```

must now **append a trailing `/` to any prefix that lacks one** before
re-validating the container, and must apply the same normalisation to **every
key of the credentials map**. Containers that still fail validation after
normalisation are dropped silently (a `tracing` warning only): `new` still
returns a resolver, the surviving containers are unaffected, and the dropped
container matches nothing — `matching_container` on a location under its prefix
returns `None`. Both halves are in scope:

* a container that reaches the resolver from a **persisted configuration**
  carrying an unseparated prefix — the only route left, since
  `VirtualChunkContainer::new` refuses to build one — is normalised, so
  `matching_container("…/foo/x")` still matches while
  `matching_container("…/foo-production/x")` no longer does, and the matched
  container reports the **separated** prefix from `url_prefix()`;
* an authorisation map written before this release, keyed by the unseparated
  prefix, still authorises the separated container — so pre-existing
  `authorize_virtual_chunk_access` maps keep working rather than failing with
  `UnauthorizedVirtualChunkContainer`.

**The persisted route is `serde`, and deserialisation stays permissive.**
`VirtualChunkContainer` derives `Deserialize` and that derive does **not** run
the constructor's checks, which is what leaves an unseparated prefix able to
reach `VirtualChunkResolver::new` at all; adding validation to `Deserialize`
would close the only route this section describes. Its wire form is the derived
one — a `url_prefix` string member and a `store` member holding an externally
tagged `ObjectStoreConfig` whose variant names are `snake_case`:

```json
{"url_prefix": "file:///data/foo", "store": {"local_file_system": "/data/foo"}}
```

A location no container covers fails with
`VirtualReferenceErrorKind::NoContainerForUrl(String)`, carrying the location.
A container that matches but has no authorisation entry fails with
`VirtualReferenceErrorKind::UnauthorizedVirtualChunkContainer(Box<VirtualChunkContainer>)`
before any I/O.

At the `Store` layer:

```rust
pub async fn set_virtual_ref(
    &self, key: &str, reference: VirtualChunkRef, validate_container: bool,
) -> StoreResult<()>;
```

with `validate_container == true` a location no container covers is rejected
with `StoreErrorKind::InvalidVirtualChunkContainer { chunk_location: String }`,
where `chunk_location` is the reference location's **URL string**; with
`false` the same reference is recorded. The parameter is positional and
mandatory in Rust at this release, exactly as before.

### 1.3 `VirtualChunkLocation` is opaque

```rust
pub struct VirtualChunkLocation(String);           // field now PRIVATE

impl VirtualChunkLocation {
    pub fn url(&self) -> &str;                     // NEW
    pub fn from_absolute_path(path: &str)
        -> Result<VirtualChunkLocation, VirtualReferenceError>;
}
```

The tuple field is no longer public: construct with `from_absolute_path` and
read with `url()`. Everything that used to read the field now reads the
accessor: `Session::matching_container(&VirtualChunkLocation)` resolves the
location through its URL, and `Session::all_virtual_chunk_locations()` yields
each virtual reference's location as that URL string.

### 1.4 `CannotParseUrl` becomes a struct variant

```rust
pub enum VirtualReferenceErrorKind {             // #[non_exhaustive]
    #[error("error parsing virtual ref URL: {url:?}")]
    CannotParseUrl {
        #[source] cause: url::ParseError,
        url: String,
    },
    ...
}
```

The variant carries the offending URL and names it in its display form. The
`#[from]` that generated `impl From<url::ParseError> for VirtualReferenceErrorKind`
is gone with the tuple form, so `url::ParseError` no longer converts with `?`.

### 1.5 A conflict solver reference must be `Send + Sync`

```rust
pub async fn rebase(
    &mut self, solver: &(dyn ConflictSolver + Send + Sync),
) -> SessionResult<()>;

pub async fn commit_rebasing<F1, F2, Fut1, Fut2>(
    &mut self,
    solver: &(dyn ConflictSolver + Send + Sync),
    rebase_attempts: u16,
    message: &str,
    properties: Option<SnapshotProperties>,
    before_rebase: F1,
    after_rebase: F2,
) -> SessionResult<SnapshotId>
where F1: Fn(u16) -> Fut1, F2: Fn(u16) -> Fut2,
      Fut1: Future<Output = ()>, Fut2: Future<Output = ()>;
```

Widening the trait object makes the returned futures `Send`, so a rebase can be
driven from a spawned task on a multi-thread runtime. `BasicConflictSolver` and
`ConflictDetector` satisfy the bound already, so call sites using them need no
change.

### 1.6 Raised default asset cache sizes

```rust
impl CachingConfig {
    pub fn num_snapshot_nodes(&self) -> u64;   // default 30_000     (was 10_000)
    pub fn num_chunk_refs(&self) -> u64;       // default 15_000_000 (was 5_000_000)
    pub fn num_transaction_changes(&self) -> u64;  // 0, unchanged
    pub fn num_bytes_attributes(&self) -> u64;     // 0, unchanged
    pub fn num_bytes_chunks(&self) -> u64;         // 0, unchanged
}
```

These are the values a repository created with no explicit caching
configuration reports through `repo.config().caching()`. An explicit
`Some(n)` still wins, including `Some(0)`, and a cache too small for the data
is advisory only: reads still return correct bytes.

The other configuration defaults this release leaves alone are the three
zero-valued cache limits above and compression:
`config::CompressionConfig::level()` is still `3` (§3, M24).

### 1.7 Garbage collection and chunk accounting gain mandatory parameters

```rust
impl GCConfig {
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        extra_roots: HashSet<SnapshotId>,
        dangling_chunks: Action,
        dangling_manifests: Action,
        dangling_attributes: Action,
        dangling_transaction_logs: Action,
        dangling_snapshots: Action,
        max_snapshots_in_memory: NonZeroU16,             // NEW
        max_compressed_manifest_mem_bytes: NonZeroUsize, // NEW
        max_concurrent_manifest_fetches: NonZeroU16,     // NEW
        dry_run: bool,                                   // NEW
    ) -> Self;

    pub fn clean_all(
        chunks_age: DateTime<Utc>,
        metadata_age: DateTime<Utc>,
        extra_roots: Option<HashSet<SnapshotId>>,
        max_snapshots_in_memory: NonZeroU16,             // NEW
        max_compressed_manifest_mem_bytes: NonZeroUsize, // NEW
        max_concurrent_manifest_fetches: NonZeroU16,     // NEW
        dry_run: bool,                                   // NEW
    ) -> Self;
}

pub async fn repo_chunks_storage(
    storage: &(dyn Storage + Send + Sync),
    storage_settings: &storage::Settings,
    asset_manager: Arc<AssetManager>,
    max_snapshots_in_memory: NonZeroU16,                 // NEW
    max_compressed_manifest_mem_bytes: NonZeroUsize,     // NEW
    max_concurrent_manifest_fetches: NonZeroU16,         // NEW
) -> RepositoryResult<u64>;
```

Behaviour:

* `dry_run == true` makes every delete phase **count** the objects and bytes it
  would remove and issue no storage delete: the object store is left exactly as
  it was. A real run with the same configuration then reports the same counts
  and removes exactly that many objects, leaving everything the surviving refs
  reach readable.
* The three tuning parameters bound memory and concurrency. They change
  throughput, never results: the same repository must report the same
  `GCSummary` and the same `repo_chunks_storage` total at
  `NonZeroUsize::MIN` / `NonZeroU16::new(1)` as at `::MAX`, and two
  `repo_chunks_storage` futures driven concurrently over one repository must
  agree with a serial run.
* `repo_chunks_storage` counts each distinct chunk exactly once, even when
  several references share ancestry, and returns the total of their lengths.
  Distinctness is per chunk **object**, not per byte string: each call to
  `Session::get_chunk_writer()` writes a new object under a fresh random id, so
  writing the same ten bytes to forty coordinates makes forty distinct chunks
  totalling four hundred bytes. Nothing de-duplicates identical content, here or
  in `GCSummary`.
* `deletes_chunks`, `deletes_manifests`, `deletes_attributes`,
  `deletes_transaction_logs` and `deletes_snapshots` keep their meaning: they
  report whether the corresponding `Action` is not `Keep`.
* `garbage_collect`, `expire`, `expire_ref`, `GCSummary`, `GCError`, `Action`,
  `ExpireRefResult`, `ExpiredRefAction` and `ExpireResult` keep their v1.0.0
  signatures.
* Only **materialised** chunks are accounted for. A chunk small enough to be
  inlined lives inside its manifest and contributes nothing to
  `repo_chunks_storage` and nothing to `GCSummary::chunks_deleted`; set
  `inline_chunk_threshold_bytes` to `Some(0)` to measure chunk objects.

### 1.8 Snapshot traversal: a rename and an element-type change

```rust
impl AssetManager {
    // NEW NAME for the v1.0.0 `snapshot_ancestry`, same element type
    pub async fn snapshot_info_ancestry(self: Arc<Self>, &SnapshotId)
        -> RepositoryResult<impl Stream<Item = RepositoryResult<SnapshotInfo>> + use<>>;

    // SAME NAME, NEW ELEMENT TYPE
    pub async fn snapshot_ancestry(self: Arc<Self>, &SnapshotId)
        -> RepositoryResult<impl Stream<Item = RepositoryResult<Arc<Snapshot>>> + use<>>;
}
```

Both walk latest-first and include the snapshot named by the argument. They
must agree on the sequence of ids they yield. `Repository::snapshot_ancestry`,
`snapshot_ancestry_arc`, `ancestry` and `ancestry_arc` keep their signatures and
still yield `SnapshotInfo`; the `_arc` in the two names is the **receiver**, so
`snapshot_ancestry(&self, &SnapshotId)` and `ancestry(&self, &VersionInfo)` take
`&self` while `snapshot_ancestry_arc(self: Arc<Self>, &SnapshotId)` and
`ancestry_arc(self: Arc<Self>, &VersionInfo)` take an `Arc<Repository>`, and all
four yield `SnapshotInfo`.

```rust
pub async fn pointed_snapshots<'a>(
    storage: &'a (dyn Storage + Send + Sync),
    storage_settings: &'a storage::Settings,
    asset_manager: Arc<AssetManager>,
    extra_roots: &'a HashSet<SnapshotId>,
) -> RepositoryResult<impl Stream<Item = RepositoryResult<Arc<Snapshot>>> + 'a>;
```

Because the stream now yields whole snapshots rather than ids, a consumer can
read each one's `manifest_files()` — `ManifestFileInfo { id, size_bytes,
num_chunk_refs }` — without a second fetch.

```rust
pub async fn all_roots<'a>(
    storage: &'a (dyn Storage + Send + Sync),
    storage_settings: &'a storage::Settings,
    extra_roots: &'a HashSet<SnapshotId>,
) -> RefResult<impl Stream<Item = RefResult<SnapshotId>> + 'a>;
```

`ops::pointed_snapshots` yields `Arc<Snapshot>` (it yielded `SnapshotId`) and is
now **deduplicating**: it walks each root's ancestry and stops as soon as it
re-sees a snapshot already yielded, so a repository whose refs share ancestry
yields each snapshot exactly once. Ids named in `extra_roots` are included and
are not exempt from the deduplication. `ops::all_roots` is unchanged: it is the
root enumeration `pointed_snapshots` walks from, and it still yields, as ids,
the snapshots the repository's references point at together with `extra_roots`.

### 1.9 `RefVersionInfo` and `resolve_ref_version`

```rust
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RefVersionInfo {
    SnapshotId(SnapshotId),
    TagRef(String),
    BranchTipRef(String),
}

impl TryFrom<&VersionInfo> for RefVersionInfo {
    type Error = (String, DateTime<Utc>);        // (branch, at) for the AsOf case
}

impl Repository {
    pub async fn resolve_ref_version(&self, version: &RefVersionInfo)
        -> RepositoryResult<SnapshotId>;
}
```

`RefVersionInfo` is `VersionInfo` minus `AsOf`: the versions that name a
reference directly and need no ancestry walk. `TryFrom` converts the three
ref-only forms and hands back the `(branch, at)` pair for `AsOf`. `VersionInfo`
itself is unchanged and `Repository::resolve_version` still accepts every form,
including `AsOf`, agreeing with `resolve_ref_version` on the ref-only ones.
`AsOf { branch, at }` selects the newest snapshot on that branch at or before
`at`, so an instant later than every commit on the branch resolves to the
branch tip. A tag, branch or snapshot id that does not exist is an error.

`RepositoryErrorKind` — which is `#[non_exhaustive]` — gains
`Other(String)` with the display form `unexpected error: {0}`.

### 1.10 Inline chunks honour partial byte ranges

A read of an inline chunk must clamp the requested `ByteRange` to the chunk's
own length, exactly as a read of a materialised chunk does. Given a ten-byte
inline chunk:

| request | result |
| --- | --- |
| `ByteRange::ALL` | all ten bytes |
| `ByteRange::bounded(2, 5)` | bytes 2..5 |
| `ByteRange::bounded(8, 100)` | bytes 8..10 — clamped, not an error, not a panic |
| `ByteRange::from_offset(7)` | bytes 7..10 |

The same request against the same content stored as a materialised chunk must
return the same bytes. The clamping itself is a public free function:

```rust
// icechunk::session
pub fn construct_valid_byte_range(request: &ByteRange, chunk_offset: u64,
                                  chunk_length: u64) -> Range<u64>;
```

so `construct_valid_byte_range(&ByteRange::bounded(8, 100), 0, 10)` is `8..10`,
the third row of the table above.

---

## 2. What this milestone preserves

Everything the earlier milestones established still holds. The verifier
re-checks one coherent path through each of them, expressed in **this**
release's surface — several of the names below changed on the way here.

### 2.1 Milestone 01 — the repository format

`format::Path`: `Path::root()` is `/`; `"/a/b".try_into()` succeeds;
`Path::new` rejects a relative path and a non-canonical one; `name()` returns
the last segment. Object ids are Crockford base32 — a `SnapshotId` is twelve
bytes and twenty characters, and `String::from(&id)` / `SnapshotId::try_from`
round-trip.

`Ref::DEFAULT_BRANCH == "main"`. `Repository::{create_branch, list_branches,
lookup_branch, delete_branch, create_tag, list_tags, lookup_tag, delete_tag}`
behave as before: tags are immutable, a deleted tag name can never be reused,
and `main` cannot be deleted.

Groups and arrays: `Session::{add_group, add_array, get_node, get_group,
get_array, list_nodes, delete_array}`; `NodeSnapshot::node_type()` returns
`NodeType::Group` or `NodeType::Array`; `user_data` is the caller's bytes.

Chunk payloads: `get_chunk_writer()` returns `ChunkPayload::Inline` when the
payload is at or below `inline_chunk_threshold_bytes` (default 512) and
`ChunkPayload::Ref` above it; `get_chunk_ref` reports which. A chunk coordinate
outside the array's grid is an error.

The Zarr key grammar through `Store`: `zarr.json` for the root group,
`<path>/zarr.json` for a node, `<path>/c/<i>/<j>/…` for a chunk; `get`, `set`,
`exists`, `delete`, `list_dir` and ranged `get` behave as documented, with
`list_dir` yielding the bare child names directly under its prefix — see §2.7
for all five signatures and for that output shape.

### 2.2 Milestone 02 — sessions, conflicts and rebase

A read-only session **records edits but never publishes them**: `add_group` and
the other hierarchy and chunk methods succeed on one, because they only touch
the session's in-memory change set, and it is `commit` that fails. (The
corpus's "a read-only session cannot do writes" in `guide/faq.md` is about what
reaches storage, not about which methods return `Ok`.) A successful `commit`
turns its session read-only, so it cannot commit twice. A session's attachment
is readable: `Session::branch()` is `Some(name)` while the session is attached to a branch
and `None` once `commit` has detached it or when the session came from
`readonly_session`, `Session::read_only()` is the complement, and
`Session::snapshot_id()` is the snapshot the session sits on — the id
`readonly_session(&VersionInfo::SnapshotId(id))` was opened at, and after a
commit the id that commit produced. Two writable sessions on one branch race,
and the loser's `commit` fails with
`SessionErrorKind::Conflict { expected_parent, actual_parent }`. `Session::rebase` with
`ConflictDetector` reports the conflict; with `BasicConflictSolver::default()`
it resolves a double chunk update in favour of the rebasing session, which can
then commit. `Session::as_bytes` / `from_bytes` round-trip a session, and
`Session::merge(other: Session)` folds one into another before a single commit.

### 2.3 Milestone 03 — expiration, garbage collection and the file format

`format::format_constants`: `SpecVersionBin::current() == SpecVersionBin::V0dot1`
with discriminant `1`; `FileTypeBin::{Snapshot = 1, Manifest = 2,
Attributes = 3, TransactionLog = 4, Chunk = 5}`; both have `TryFrom<u8>` that
rejects an unknown code.

`ops::gc::expire(storage, settings, asset_manager, older_than, expired_branches,
expired_tags)` releases snapshots older than a threshold and edits the oldest
survivor in place, leaving the branch tip resolvable and its data readable. Its
two `ExpiredRefAction` arguments decide what happens to a reference whose whole
ancestry expired: `Ignore` leaves it, `Delete` removes it and reports it in
`ExpireResult::deleted_refs`, a `HashSet<Ref>` whose members are the
`refs::Ref::{Tag, Branch}(String)` values naming the references removed — except
`main`, which is never deleted. `garbage_collect` removes
exactly the objects its `GCSummary` counts, and everything the surviving refs
reach still reads back byte for byte.

### 2.4 Milestone 04 — the FlatBuffers format and the error split

A snapshot's buffer carries the FlatBuffers file identifier `Ichk` at bytes
4..8. `Snapshot::from_buffer(Vec<u8>)` verifies before granting access: it
round-trips a real buffer, preserving `id()`, `message()`, `parent_id()` and
`len()`, and rejects a corrupt one. Errors are `ICError<Kind>` — match
`Err(XError { kind: XErrorKind::Variant { .. }, .. })`, never a bare enum. The
two the inherited path exercises are the ones milestone 04 named when it split
the errors and which are unchanged here: reading a node that is not there is
`SessionErrorKind::NodeNotFound { path, .. }`, whose `path` is the path that was
looked up, and a reference lookup that finds nothing is
`RepositoryErrorKind::Ref(_)`. No other variant's identity is asserted anywhere
in this milestone: everything else is checked as a category, with `is_err()`.
Zarr metadata bytes are stored verbatim in `NodeSnapshot::user_data`, so an
extension key no Icechunk version understands survives a `Store::set` /
`Store::get` round trip unchanged. `Session::status` and `Repository::diff`
report `new_groups`, `new_arrays` and `updated_chunks` as `Diff`.

### 2.5 Milestone 05 — the command-line interface

The `icechunk` binary is behind `--features cli`; the verifier builds it with

```
cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk
```

and drives the resulting process. Its grammar:

```
icechunk repo create <alias>
icechunk snapshot list <REPO> [-n <N>] [-b|--branch <BRANCH>]
icechunk config init [-f|--force]
icechunk config add <alias>
icechunk config list
```

`-n` defaults to **10** and `-b/--branch` defaults to **`main`**, so
`snapshot list <alias>` with neither flag lists up to ten snapshots of `main`.

Exit statuses: `0` on success and for `--help`; **`2`** for a usage error (no
subcommand, an unknown subcommand); **`1`** for a runtime failure, with the
message on stderr. `--help` prints `Usage: icechunk …` and lists `repo`,
`snapshot` and `config`; `snapshot list --help` documents `--branch`.

Configuration lives at `$XDG_CONFIG_HOME/icechunk/cli-config.yaml`, read through
`dirs::config_dir()`. It is YAML with **externally tagged** enum syntax:

```yaml
repos:
  local1: !LocalFileSystem
    path: /some/dir
    config: {}
```

`config list` prints the configuration back as YAML, so a declared
repository's alias and its kind both appear in its output. A missing or
unreadable file reads as an empty configuration, so `config list` still succeeds
and prints `repos:` with no entries. `repo create <alias>` creates the
repository and prints a line naming the alias; the repository directory then
contains `refs/` and `snapshots/`. `snapshot list <alias>` prints one line per
snapshot, newest first, each the `Debug` form of `SnapshotInfo` beginning
`SnapshotInfo {`; a fresh repository has exactly one, whose message is
`Repository initialized`. `-n` caps the number of lines. An alias the configuration does not
declare fails the same way wherever it is given — `snapshot list <alias>` and
`repo create <alias>` alike — with exit `1` and `Repository not found in config`
on stderr; an unknown branch fails with exit `1`; creating a repository over a
non-clean prefix fails with exit `1`.

`config init` and `config add` are **interactive**: they prompt for an alias, a
repository kind and its location on a terminal, so neither is usable
non-interactively and **no requirement group runs them** — see §5. They are listed
above because the grammar is part of the inherited surface, not because this
milestone observes them.

### 2.6 Milestone 06 — manifest splitting and the fixed first snapshot

`format::manifest::uniform_manifest_split_edges(num_chunks, &split_size)`
returns the uniform edge list with a ragged tail: `(24, 12)` gives
`[0, 12, 24]`, `(25, 12)` gives `[0, 12, 24, 25]`. `ManifestSplits::from_edges`
builds the N-D grid; `position` and `find` locate a coordinate and return
`None` outside the grid; `ManifestExtents::new(from, to)` has an **exclusive**
upper bound.

`ManifestSplittingConfig::with_size(n)`, placed on
`ManifestConfig { splitting: Some(..) }`, splits an array's chunk space into
cells of `n` chunks per axis and writes one manifest per non-empty cell: ten
chunks at `with_size(4)` produce three manifests whose `num_chunk_refs` sum to
ten. Splitting is invisible to readers — every chunk still reads back.

`Snapshot::INITIAL_SNAPSHOT_ID` is the fixed constant whose Crockford
base32 form is `1CECHNKREP0F1RSTCMT0`, and
`Snapshot::INITIAL_COMMIT_MESSAGE` is `Repository initialized`; a fresh
repository's `main` points at it. `ops::manifests::rewrite_manifests(&repo,
branch, message, properties)` rewrites the tip's manifests under the current
splitting configuration, advances the branch, and records a
`splitting_config` key in the resulting snapshot's metadata.

### 2.7 The inherited signatures the verifier calls

§§2.1–2.6 state inherited *behaviour*. The groups that check it reach that
behaviour through inherited *names*, and those are listed here so that no part
of the surface the verifier touches has to be inferred. **Every item below is
unchanged at this release** — §3's closing paragraph already pins the types that
did not move, and these are the call-site spellings that go with them. A
candidate who migrates the §3 items and leaves the rest alone has them already;
re-spelling one of them is what breaks a group.

Construction and repositories:

```rust
// crate root re-exports
use icechunk::{Repository, Store, Storage, ObjectStoreConfig, RepositoryConfig,
               new_in_memory_storage, new_local_filesystem_storage};

impl Repository {
    pub async fn create(Option<RepositoryConfig>, Arc<dyn Storage + Send + Sync>,
                        HashMap<String, Option<Credentials>>) -> RepositoryResult<Self>;
    pub async fn open(Option<RepositoryConfig>, Arc<dyn Storage + Send + Sync>,
                      HashMap<String, Option<Credentials>>) -> RepositoryResult<Self>;
    pub async fn writable_session(&self, branch: &str) -> RepositoryResult<Session>;
    pub async fn readonly_session(&self, &VersionInfo) -> RepositoryResult<Session>;
    pub async fn lookup_snapshot(&self, &SnapshotId) -> RepositoryResult<SnapshotInfo>;
    pub fn asset_manager(&self) -> &Arc<AssetManager>;
    pub async fn reset_branch(&self, branch: &str, &SnapshotId) -> RepositoryResult<()>;

    pub async fn create_branch(&self, branch: &str, &SnapshotId) -> RepositoryResult<()>;
    pub async fn delete_branch(&self, branch: &str) -> RepositoryResult<()>;
    pub async fn list_branches(&self) -> RepositoryResult<BTreeSet<String>>;
    pub async fn lookup_branch(&self, branch: &str) -> RepositoryResult<SnapshotId>;
    pub async fn create_tag(&self, tag: &str, &SnapshotId) -> RepositoryResult<()>;
    pub async fn delete_tag(&self, tag: &str) -> RepositoryResult<()>;
    pub async fn list_tags(&self) -> RepositoryResult<BTreeSet<String>>;
    pub async fn lookup_tag(&self, tag: &str) -> RepositoryResult<SnapshotId>;
}
```

The third argument of `create` / `open` is the authorisation map §1.2 normalises
(`PROFILE.md` R6-3). `asset_manager()` is how the `Arc<AssetManager>` that §1.7
and §1.8 take as a parameter is obtained, and
`AssetManager::fetch_snapshot(&SnapshotId) -> RepositoryResult<Arc<Snapshot>>`
is how a snapshot is read back; the type lives in the crate-level module
`icechunk::asset_manager`. `writable_session` and `readonly_session` are the two
ways a `Session` is obtained — `Repository` itself has no data methods — and
`lookup_snapshot` reads one snapshot's `SnapshotInfo` by id.

The reference methods §2.1 describes return the shapes above: `list_branches`
and `list_tags` hand back a `BTreeSet<String>` of bare names, so membership is
`repo.list_branches().await?.contains("main")`, and `lookup_branch` /
`lookup_tag` hand back the `SnapshotId` the reference points at.

Sessions and chunks:

```rust
impl Session {
    pub fn read_only(&self) -> bool;
    pub fn branch(&self) -> Option<&str>;
    pub fn snapshot_id(&self) -> &SnapshotId;
    pub fn has_uncommitted_changes(&self) -> bool;
    pub async fn add_array(&mut self, Path, ArrayShape, Option<Vec<DimensionName>>,
                           user_data: Bytes) -> SessionResult<()>;
    pub async fn set_chunk_ref(&mut self, Path, ChunkIndices, Option<ChunkPayload>)
        -> SessionResult<()>;                 // the writer half of get_chunk_ref
    pub async fn get_chunk_reader(&self, &Path, &ChunkIndices, &ByteRange)
        -> SessionResult<Option<Pin<Box<dyn Future<Output = SessionResult<Bytes>> + Send>>>>;
    pub async fn get_chunk_ref(&self, &Path, &ChunkIndices)
        -> SessionResult<Option<ChunkPayload>>;

    // NOT a write method: it returns the closure that does the write.
    pub fn get_chunk_writer(&self)
        -> impl FnOnce(Bytes) -> Pin<Box<dyn Future<Output = SessionResult<ChunkPayload>> + Send>>;

    pub async fn add_group(&mut self, path: Path, definition: Bytes) -> SessionResult<()>;
    pub async fn delete_array(&mut self, path: Path) -> SessionResult<()>;
    pub async fn get_node(&self, &Path) -> SessionResult<NodeSnapshot>;
    pub async fn get_group(&self, &Path) -> SessionResult<NodeSnapshot>;
    pub async fn get_array(&self, &Path) -> SessionResult<NodeSnapshot>;
    pub async fn list_nodes(&self)
        -> SessionResult<impl Iterator<Item = SessionResult<NodeSnapshot>> + '_>;
}

// free function in `icechunk::session`, the other half of get_chunk_reader
pub async fn get_chunk(reader: Option<Pin<Box<…>>>) -> SessionResult<Option<Bytes>>;

impl Store {                                   // `icechunk::store`
    pub async fn from_session(Arc<tokio::sync::RwLock<Session>>) -> Store;
    pub fn session(&self) -> Arc<tokio::sync::RwLock<Session>>;

    pub async fn get(&self, key: &str, &ByteRange) -> StoreResult<Bytes>;
    pub async fn set(&self, key: &str, value: Bytes) -> StoreResult<()>;
    pub async fn exists(&self, key: &str) -> StoreResult<bool>;
    pub async fn delete(&self, key: &str) -> StoreResult<()>;
    pub async fn list_dir(&self, prefix: &str)
        -> StoreResult<impl Stream<Item = StoreResult<String>> + Send>;
}
```

`get_chunk_writer` is not itself the write: it returns a closure, so a chunk is
written `session.get_chunk_writer()(bytes).await?` and the `ChunkPayload` that
comes back is what `set_chunk_ref` stores. `get_node`, `get_group` and
`get_array` all return a `NodeSnapshot`; the latter two additionally fail when
the node is of the other kind. `list_nodes` yields an `Iterator`, not a
`Stream`.

`Store::from_session` is the only constructor the verifier uses, and
`Store::session()` hands the session back so a commit can be taken through the
same lock. `Store::list_dir(prefix)` yields the **bare child names** directly
under that prefix rather than whole keys — over a root group holding one array,
`list_dir("/")` yields `array` and `zarr.json` — and its order is not specified,
so the verifier sorts before comparing. The shape types `add_array` takes are
`format::snapshot::ArrayShape::new(impl IntoIterator<Item = (u64, u64)>)
-> Option<ArrayShape>` — one `(array_length, chunk_length)` pair per axis, `None`
if any chunk length is zero — and `format::snapshot::DimensionName`, which the
verifier always passes as `None`.

Virtual chunks:

```rust
impl VirtualChunkContainer {
    pub fn url_prefix(&self) -> &str;                    // §1.1
}

impl VirtualChunkResolver {
    pub fn matching_container(&self, chunk_location: &str)
        -> Option<&VirtualChunkContainer>;               // §1.2
    pub async fn fetch_chunk(&self, chunk_location: &str, range: &Range<u64>,
                             checksum: Option<&Checksum>)
        -> Result<Bytes, VirtualReferenceError>;
}

impl RepositoryConfig {
    pub fn set_virtual_chunk_container(&mut self, VirtualChunkContainer);
}

pub struct VirtualChunkRef {                 // format::manifest
    pub location: VirtualChunkLocation, pub offset: u64,
    pub length: u64, pub checksum: Option<Checksum>,
}

pub enum ObjectStoreConfig {                 // the three variants used here
    InMemory, LocalFileSystem(PathBuf), S3(S3Options), …
}
pub struct S3Options {
    pub region: Option<String>, pub endpoint_url: Option<String>,
    pub anonymous: bool, pub allow_http: bool, pub force_path_style: bool,
}

pub enum ChunkPayload {                      // format::manifest; also a constructor
    Inline(Bytes), Virtual(VirtualChunkRef), Ref(ChunkRef),
}
```

`url_prefix()` returns a borrowed `&str` and `matching_container` a borrowed
`Option<&VirtualChunkContainer>`, so the prefix a matched container reports is
compared as `resolver.matching_container(url).map(|c| c.url_prefix())`.

Configuration, references, conflicts and manifests:

* `config::CachingConfig`'s five public fields are `Option<u64>` and carry the
  same names as the accessors in §1.6 (`num_snapshot_nodes`, `num_chunk_refs`,
  `num_transaction_changes`, `num_bytes_attributes`, `num_bytes_chunks`), so an
  explicit setting is written as `CachingConfig { num_chunk_refs: Some(7), .. }`.
* `refs::Ref::{Tag(String), Branch(String)}`, and
  `refs::update_branch(&dyn Storage + Send + Sync, &storage::Settings, name: &str,
  new_snapshot: SnapshotId, current_snapshot: Option<&SnapshotId>) -> RefResult<()>`,
  which is how a branch is moved without a session.
* `conflicts::basic_solver::BasicConflictSolver` and
  `conflicts::detector::ConflictDetector` — the module paths §1.5 and §2.2 refer
  to by name.
* `format::manifest::{ManifestExtents::contains(&[u32]) -> bool,
  ManifestSplits::len() -> usize}`, used by §2.6's grid checks, and the two
  constructors and two lookups that go with them:
  `ManifestExtents::new(from: &[u32], to: &[u32]) -> ManifestExtents`,
  `ManifestSplits::from_edges(impl IntoIterator<Item = Vec<u32>>) -> ManifestSplits`
  — one edge vector per axis —
  `ManifestSplits::position(&ChunkIndices) -> Option<usize>` and
  `ManifestSplits::find(&ChunkIndices) -> Option<&ManifestExtents>`.
* `RepositoryConfig` is `Default` and is constructed as a struct literal over
  public `Option` fields. The three the verifier sets by name are
  `inline_chunk_threshold_bytes: Option<u16>`, `caching: Option<CachingConfig>`
  and `manifest: Option<ManifestConfig>`; the remaining ones are
  `get_partial_values_concurrency`, `compression`, `storage` and
  `virtual_chunk_containers`. So a cache setting is written
  `RepositoryConfig { caching: Some(CachingConfig { .. }), ..Default::default() }`
  and a splitting setting
  `RepositoryConfig { manifest: Some(ManifestConfig { splitting: Some(..), ..Default::default() }), ..Default::default() }`
  — `config::ManifestConfig` is `Default` with fields `preload` and `splitting`
  (§2.6). `inline_chunk_threshold_bytes()`, `caching()` and `manifest()` are the
  same-named accessors that apply the defaults §1.6 and §2.1 state.
* `format::ChunkIndices` is a tuple struct with a public field —
  `pub struct ChunkIndices(pub Vec<u32>)` — so a chunk coordinate is written
  `ChunkIndices(vec![0])`.
* `format::Path` and `format::SnapshotId` both implement `Display`, so
  `to_string()` gives `/a/b` and the twenty-character Crockford base32 id.
* `repository::VersionInfo::AsOf` is a **struct** variant whose fields are
  `branch: String` and `at: DateTime<Utc>`, written
  `VersionInfo::AsOf { branch: "main".to_string(), at }` (§1.9).
* `session::SessionErrorKind::NodeNotFound { path, message }` and
  `repository::RepositoryErrorKind::Ref(RefErrorKind)` — see §2.4.
* `format::Path::starts_with(&Path) -> bool` and
  `format::SnapshotId::random() -> SnapshotId` (the id constructor used to name
  a snapshot that does not exist).
* `format::snapshot::NodeSnapshot`'s `path: Path` and `user_data: Bytes` are
  public fields, read directly rather than through an accessor.
* `format::snapshot::Snapshot::bytes(&self) -> &[u8]` is the serialised buffer
  `from_buffer` consumes (§2.4). `format::snapshot::SnapshotInfo`'s public
  fields are `id`, `parent_id`, `flushed_at`, `message`, `metadata` and
  `manifests`, and `SnapshotInfo::is_initial(&self) -> bool` is true exactly
  when `parent_id` is `None`.
* `format::manifest::ManifestExtents` derives `PartialEq`, so a grid lookup is
  compared as `splits.find(&coord) == Some(&extent)` (§2.6).
* Garbage collection and expiration, whose behaviour §1.7 and §2.3 state:

  ```rust
  pub async fn garbage_collect(storage: &(dyn Storage + Send + Sync),
                               storage_settings: &storage::Settings,
                               asset_manager: Arc<AssetManager>,
                               config: &GCConfig) -> GCResult<GCSummary>;

  pub enum Action { Keep, DeleteIfCreatedBefore(DateTime<Utc>) }   // ops::gc

  pub struct GCSummary {                       // derives PartialEq, Eq, Default
      pub bytes_deleted: u64, pub chunks_deleted: u64,
      pub manifests_deleted: u64, pub snapshots_deleted: u64,
      pub attributes_deleted: u64, pub transaction_logs_deleted: u64,
  }

  pub struct ExpireResult {
      pub released_snapshots: HashSet<SnapshotId>,
      pub edited_snapshots: HashSet<SnapshotId>,
      pub deleted_refs: HashSet<Ref>,
  }

  // the single-reference half `expire` is built on; §1.7 pins both unchanged
  pub async fn expire_ref(storage: &(dyn Storage + Send + Sync),
                          storage_settings: &storage::Settings,
                          asset_manager: Arc<AssetManager>,
                          reference: &Ref,
                          older_than: DateTime<Utc>)
      -> Result<ExpireRefResult, GCError>;          // GCResult<ExpireRefResult>

  pub enum ExpireRefResult {
      NothingToDo { ref_is_expired: bool },
      Done { released_snapshots: HashSet<SnapshotId>,
             edited_snapshot: SnapshotId,
             ref_is_expired: bool },
  }
  ```

  `ref_is_expired` is true when the reference's *own* snapshot is older than
  `older_than`, whichever variant carries it.

* Two methods of the `Storage` trait itself, called on the `Arc<dyn Storage +
  Send + Sync>` the storage constructors return:
  `fn default_settings(&self) -> storage::Settings`, which supplies the
  `&storage::Settings` §1.7, §1.8 and §2.3 take, and
  `async fn list_chunks(&self, &storage::Settings) -> StorageResult<BoxStream<
  StorageResult<storage::ListInfo<ChunkId>>>>`, which is how a driver counts
  the chunk objects that exist.
* `ConflictDetector` is a **unit struct**: the value is written
  `ConflictDetector`, not `ConflictDetector::new()` or
  `ConflictDetector::default()`. `BasicConflictSolver` implements `Default`.

---

## 3. Migration from the previous checkpoint

Thirty-two Rust items are listed below, in the tables that follow: twenty-one
previously-public items changed name, signature, behaviour or wire format
between the previous checkpoint and this one (M1–M12, M19, M20, M25–M31); six
are new (M13–M18); one is internal (M32); and four are pinned here as
**unchanged** so you can check you have not moved them (M21–M24). Nothing was
removed from the module tree; the two removals are a public field and a `From`
impl.

A final table, **Newly in scope**, is different in kind and is not one of the
thirty-two: nothing about the behaviour changed, only whether a group of this
milestone reads it.

**Renamed**

| # | before | after |
| --- | --- | --- |
| M1 | `AssetManager::snapshot_ancestry` yielding `SnapshotInfo` | `AssetManager::snapshot_info_ancestry`, same element type |

**Same name, changed signature or element type**

| # | item | change |
| --- | --- | --- |
| M2 | `AssetManager::snapshot_ancestry` | yields `Arc<Snapshot>` instead of `SnapshotInfo` — **it still compiles at the call site and silently changes type** |
| M3 | `ops::pointed_snapshots` | yields `Arc<Snapshot>` instead of `SnapshotId`, and deduplicates across refs |
| M4 | `Session::rebase` | `solver: &dyn ConflictSolver` → `&(dyn ConflictSolver + Send + Sync)` |
| M5 | `Session::commit_rebasing` | same widening on its second argument |
| M6 | `ops::gc::GCConfig::new` | 6 arguments → 10 |
| M7 | `ops::gc::GCConfig::clean_all` | 3 arguments → 7 |
| M8 | `ops::stats::repo_chunks_storage` | 3 arguments → 6 |
| M9 | `format::manifest::VirtualChunkLocation` | `(pub String)` → `(String)` |
| M10 | `VirtualReferenceErrorKind::CannotParseUrl` | tuple variant → struct variant `{ cause, url }` |

**Removed**

| # | item |
| --- | --- |
| M11 | the public tuple field `VirtualChunkLocation.0` — read `url()`, construct with `from_absolute_path` |
| M12 | `impl From<url::ParseError> for VirtualReferenceErrorKind`, and with it the `?` conversion into `VirtualReferenceError` |

**Added**

| # | item |
| --- | --- |
| M13 | `pub enum repository::RefVersionInfo { SnapshotId, TagRef, BranchTipRef }` |
| M14 | `impl TryFrom<&VersionInfo> for RefVersionInfo { type Error = (String, DateTime<Utc>); }` |
| M15 | `Repository::resolve_ref_version(&RefVersionInfo) -> RepositoryResult<SnapshotId>` |
| M16 | `VirtualChunkLocation::url(&self) -> &str` |
| M17 | `RepositoryErrorKind::Other(String)` on a `#[non_exhaustive]` enum |
| M18 | `AssetManager::snapshot_info_ancestry` (see M1) |

**Default-value changes**

| # | item | before | after |
| --- | --- | --- | --- |
| M19 | `CachingConfig::num_snapshot_nodes()` | `10_000` | `30_000` |
| M20 | `CachingConfig::num_chunk_refs()` | `5_000_000` | `15_000_000` |
| M21 | `CachingConfig::num_transaction_changes()` | `0` | `0`, unchanged |
| M22 | `CachingConfig::num_bytes_attributes()` | `0` | `0`, unchanged |
| M23 | `CachingConfig::num_bytes_chunks()` | `0` | `0`, unchanged |
| M24 | `CompressionConfig::level()` | `3` | `3`, unchanged |
| M25 | `initialize_tracing(None)` (feature `logs`) | the tracing default | floors the filter at `WARN` |

**Serialisation-format changes**

| # | item | change |
| --- | --- | --- |
| M26 | `config::GcsStaticCredentials` | internally tagged → adjacently tagged, `content = "__field0"` |
| M27 | `config::AzureStaticCredentials` | internally tagged → adjacently tagged, `content = "__field0"` |

**Message-text changes**

| # | item |
| --- | --- |
| M28 | `CannotParseUrl` display gains the URL: `error parsing virtual ref URL: {url:?}` |
| M29 | `NoContainerForUrl` and `UnauthorizedVirtualChunkContainer` documentation URLs drop the `/icechunk-python` segment |
| M30 | `SessionErrorKind::InvalidSnapshotTimestamp` display mentions machine clock drift; field names unchanged |
| M31 | `VirtualChunkContainer::new` gains `VirtualChunkContainer url_prefix must end in a / character` |

**Module tree**

| # | item |
| --- | --- |
| M32 | a private `stream_utils` module is added to `lib.rs`; its contents are `pub(crate)` and unreachable from outside the crate |

**Newly in scope** (behaviour unchanged since the previous checkpoint; what
changed is that a group now reads it, so an implementation that was correct and
out of scope there can fail here)

| # | item | then | now |
| --- | --- | --- | --- |
| N1 | the CLI's unknown-alias failure, on **stderr** | milestone 05 fixed the stream and the exit status and said in as many words that *what* it writes is **out of scope** — no group read the wording | §2's recap makes the text contract: `snapshot list <alias>` and `repo create <alias>` on an alias the configuration does not declare exit `1` and write `Repository not found in config` on stderr. A group asserts stderr **contains** that string, so surrounding context, a prefix such as `Error:` and a trailing cause chain are all fine; the phrase itself must be present |

Nothing else about the command-line interface changed at this checkpoint: the
stream rules, the exit statuses and the stdout wording are milestone 05's.

Unchanged despite the milestone wording: `format/mod.rs`, `format/snapshot.rs`
(including `ManifestFileInfo`), `ops/manifests.rs`, every `store.rs` public
signature, `RepositoryConfig`, `VersionInfo`, `SnapshotInfo`, `GCSummary`,
`ExpireResult`, the `Storage` trait, every `storage::` constructor, and the
on-disk format specification.

---

## 4. Test environment

* `#[tokio::test]`, with `flavor = "multi_thread"` where concurrency is the
  point. Upstream's `#[icechunk_macros::tokio_test]` is not available to an
  external crate.
* No network, no cloud endpoint, no MinIO, no credential from the environment,
  no wall-clock sleep. Repository storage is `new_in_memory_storage()` or
  `new_local_filesystem_storage()` over a temporary directory; virtual chunks
  address `file://` URLs inside a temporary directory.
* The command-line groups run the candidate's own binary as a subprocess and
  redirect `XDG_CONFIG_HOME` into a temporary directory.
* Types that cross the public API must keep the versions the crate manifest
  pins: `tokio` (`rt-multi-thread`, `macros`), `futures`, `bytes`, `chrono`,
  `url`, `serde`, `serde_json`, `thiserror`.
* The verifier builds your workspace with `cargo build --locked --offline`, and
  the binary with `cargo build --manifest-path /app/icechunk/Cargo.toml
  --features cli --bin icechunk` under the same flags. Each build falls back, in
  order, to `--locked` alone — which downloads the exact versions your lock pins
  — and then to `--offline` alone. So a `Cargo.lock` consistent with your
  manifests always builds; a drifted lock builds only if the versions cargo
  would pick are already in the verifier's cache, which is a rescue path and not
  a guarantee. Keeping `Cargo.lock` committed and consistent with the manifests
  is a workspace obligation, and the next milestone inherits it. The verifier's
  own drivers are built from a lockfile of their own and never take yours.

---

## 5. What is out of scope, and why

The milestone's headline is "the asynchronous API". **That API does not exist in
the Rust crate, and the verifier drives the Rust crate, so it is out of scope.**

* **The asynchronous twin.** The Rust crate has been fully `async` since long
  before the previous checkpoint: every I/O method on `Repository`, `Session`,
  `Store`, `ops::gc` and `ops::stats` was already an `async fn`. The milestone
  adds a second, `async def`, PyO3-level entry point beside each existing
  synchronous wrapper in `icechunk-python`, named with an `_async` postfix.
  There is no `_async` item anywhere under `icechunk/src/`. The sync wrapper
  blocks on the shared tokio runtime and the async wrapper hands the *same*
  Rust future to the Python event loop, which is why a synchronous and an
  asynchronous run over one repository produce identical object stores — but
  there is no sync/async duality in Rust to observe, and no Rust symbol whose
  presence or absence could stand in for one. Implement it for the Python
  package if you build one; nothing here checks it. Grepping the crate for
  `_async` would be a symbol check, not a behavioural one, and is not what this
  verifier does.
* **`ManifestFileInfo` property accessors.** `icechunk/src/format/snapshot.rs`
  is byte-identical across the two checkpoints and `ManifestFileInfo`'s three
  fields have been `pub` since before the previous one. The change is
  `#[pyo3(get)]` on the Python wrapper, correcting a stub that already declared
  the properties. There is nothing to add in Rust, so asserting the fields would
  be a non-regression check that passes at both checkpoints and could not
  discriminate this milestone. What is out of scope is the **Python** accessors
  alone: the Rust fields are `pub` and §1.8's group does read `num_chunk_refs`
  and `size_bytes` off `Snapshot::manifest_files()` — as a consumer of this
  release's new `Arc<Snapshot>` stream, which is what that group discriminates,
  not as a check on `ManifestFileInfo` itself.
* **Container validation on by default when a virtual reference is set.** Rust
  has no default arguments: `validate_container` is positional and mandatory on
  `Store::set_virtual_ref` and `Store::set_virtual_refs` at both checkpoints.
  The default flip from `False` to `True` is in
  `icechunk-python/python/icechunk/store.py`. What *is* in scope is the behaviour
  the flag selects — see §1.2.
* **The manifest-cache-too-small warning.** It is emitted by a private method
  through `tracing::warn!`: no return value, no field on any returned struct, no
  error variant, nothing in the public Rust API. Checking it would mean
  installing a `tracing` subscriber and matching the text of a log line, which
  makes a message string normative — the wrong kind of requirement, and one this
  verifier deliberately does not create. Emit the warning anyway if you follow
  the changelog; it is advisory and changes no result. What *is* in scope is the
  configuration half, §1.6: the raised defaults, the fact that an explicit
  setting still wins, and that a tiny cache does not change the data that comes
  back.
* **The constructor's canonical-path check.** `VirtualChunkContainer::new`
  rejects a `file://` prefix whose path segments include `.` or `..`, but the
  `url` crate normalises those segments away while parsing a `file:` URL, so no
  input can reach the check: a prefix written with `..` is either refused
  earlier for a different reason or accepted in its collapsed form. There is
  nothing an external driver can observe, so no group asserts it. Implement it
  to match the release; §1.1 states the observable half.
* **The compile-time-only properties of the migrated types.** Three statements
  in §1.3, §1.4, §1.9 and §3 are observable only by *failing to compile*: that
  `VirtualChunkLocation`'s tuple field is no longer public (M11), that
  `impl From<url::ParseError> for VirtualReferenceErrorKind` is gone so
  `url::ParseError` no longer converts with `?` (M12), and that
  `VirtualReferenceErrorKind` and `RepositoryErrorKind` are `#[non_exhaustive]`.
  An out-of-tree driver observes a property like that only through a
  `compile_fail` harness, and this verifier compiles one test target per group,
  so a deliberate compile failure would be indistinguishable from a broken
  workspace. They are requirements — make them — but no group asserts them. What *is* asserted is the positive half each one leaves behind:
  `url()` and `from_absolute_path` in §1.3, and `CannotParseUrl`'s struct fields
  and display in §1.4.
* **The interactive command-line subcommands.** `config init [-f|--force]` and
  `config add <alias>` prompt for an alias, a repository kind and a location on
  a terminal (see §2.5). There is no non-interactive spelling of either, so no
  group runs them and neither their prompts nor their written configuration is
  in scope. `repo create`, `snapshot list`, `config list` and `--help` are the
  scriptable surface and are the ones the inherited group drives.
* **§3's M25, M26, M27, M29, M30 and M32.** They are listed in the migration
  table because you should make them, and they are out of scope, each for a
  stated reason. **M25** (`initialize_tracing(None)` floors the filter at
  `WARN`) sits behind the non-default `logs` feature, returns nothing and
  changes no value: observing it means installing a `tracing` subscriber and
  inspecting a filter, the same log-text class as the cache warning above.
  **M26/M27** re-tag two cloud-credential enums; the verifier has no cloud
  endpoint and never constructs those credentials, and the only thing left to
  check would be a serde round-trip of a value that never crosses its boundary —
  and pinning the wire shape exactly would make two undocumented serde tag names
  normative. **M29** and **M30** are message-text changes, and this verifier
  does not make message wording normative; the only message text matched
  anywhere is §1.1's `must end in a / character` substring, §1.4's
  `CannotParseUrl` display naming the URL it could not parse, §1.9's
  `RepositoryErrorKind::Other` display, and §2.5's documented CLI strings,
  because those four are stated here as contract. **M32**'s `stream_utils` module is
  `pub(crate)` and unreachable from an external driver by construction.
* **Everything else in `icechunk-python`**: the documented `_async` naming rule,
  "only I/O methods have an async version", "sync and async may be called on the
  same instance", the Zarr-sharding guidance in the FAQ and the async how-to
  page. All Python-level, all outside this task's scope.
* **Support for Ceph Object Gateway** (quoted-ETag handling in the S3 storage
  backend) and **GCS request retries** need a live S3-compatible or Google
  endpoint. This verifier has no network, so neither is in scope.
* **The low-latency concurrent-write improvement** is a performance change with
  no functional assertion attached, and is out of scope.
