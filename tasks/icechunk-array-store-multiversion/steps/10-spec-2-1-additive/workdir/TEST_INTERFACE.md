# TEST_INTERFACE.md — step 10, "spec 2.1"

The public contract an external integration test drives at this milestone. It is
self-contained: everything a hidden test may name appears here or in the
specification corpus under `/app/icechunk-spec/`, which this document cites by
document and heading.

Signatures below were read off the release this milestone implements. Where a
signature and a prose description disagree, the signature is the contract.

---

## 1. Workspace and crate identity

Unchanged from the previous milestone except for the version numbers.

* Cargo workspace at `/app`, `resolver = "2"`, `edition = "2024"`,
  `default-members = ["icechunk"]`. Nine members:
  `icechunk`, `icechunk-types`, `icechunk-format`, `icechunk-storage`,
  `icechunk-s3`, `icechunk-arrow-object-store`, `icechunk-python`,
  `icechunk-macros`, `icechunk-js`.
* The crate under check is `icechunk` at `/app/icechunk`, `version = "2.1.0"`,
  `license = "Apache-2.0"`. Every internal crate is at `2.1.0`.
* `Cargo.lock` is committed at the workspace root and pins `icechunk 2.1.0`.
* `workspace.package.rust-version` is **`1.91.1`**. It was *lowered* from
  `1.94.0` at the previous milestone; do not raise it back.
* `icechunk` default features:
  `["s3", "object-store-s3", "object-store-gcs", "object-store-azure",
  "object-store-http", "object-store-fs", "redirect"]`.
  Non-default: `logs`, `otel` (new at this milestone), `cli`, `shuttle`,
  `napi-send-contract`. `[lib] bench = false`.
* Every symbol named in this document must be reachable at its stated
  `icechunk::` path with the **default** feature set, except the CLI binary
  (§9) which needs `--features cli`.

### How the verifier builds

1. `cargo build` at `/app` (default members: the `icechunk` library).
2. `cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk`
   for the groups that run the binary. Two do: the command-line group of §9.3
   and the single end-to-end group, which walks one path through every
   milestone and so runs the CLI too. A failure of this build costs **both** of
   them and nothing else; a failure of (1) costs every group at once.
3. A verifier-owned driver crate **outside `/app`** depends on
   `icechunk = { path = "/app/icechunk" }` plus `bytes`, `chrono`, `futures`,
   `serde_json`, `serde_yaml_ng`, `tempfile` and `tokio`, and is always compiled
   `--locked`. Consequences:
   * The driver uses **only** `icechunk::` paths. A symbol that moves to a
     sibling crate without an `icechunk::` re-export is gone as far as the
     verifier is concerned — and so is one whose signature names a type from a
     crate `icechunk` does not re-export (§11).
   * Do not add a dependency that only resolves with a newer `rust-version`.

   **How the driver's dependencies are resolved.** The verifier never splices
   one lockfile into the other side's manifest. It picks one internally
   consistent resolution *before* compiling any driver code, by building the
   dependency graph alone, trying in order:

   1. **a task-owned lockfile**, built `--offline --locked`, so nothing is
      resolved and nothing is downloaded;
   2. **your `/app/Cargo.lock`** as the sole seed, completed by a single
      resolver run that may add only the driver crate's own dependencies, then
      frozen with `--locked`;
   3. a complete re-resolution of both manifests together, frozen the same way.

   **All three are supported and which one is chosen is out of scope.** The
   task-owned lockfile of (1) was resolved against this release's *upstream*
   manifests, so it is taken only where your manifests agree with them — which
   is why leaving the manifests and `Cargo.lock` as you found them is the fast
   path rather than an obligation. What (2) needs from you is only that
   `/app/Cargo.lock` stays a **regular, committed file** that cargo maintains:
   hand-editing it, deleting it or replacing it with a symlink makes the
   verifier skip (2) entirely. A manifest or lockfile that *no* resolution can
   satisfy costs every requirement group at once.

---

## 2. The spec-2.1 change, in one field

Normative source: `/app/icechunk-spec/spec.md` (spec version 2.1) —
`## Changes from spec version 2 to 2.1` and `##### \`SnapshotInfo\``;
`/app/icechunk-spec/reference/version-policy.md` — `## On-Disk Format` and
`### Compatibility`; `/app/icechunk-spec/design/016-expired-transaction-logs.md`.

### 2.1 The field

`icechunk-format/flatbuffers/repo.fbs`, table `SnapshotInfo`, gains exactly one
optional vector, appended last:

```
pruned_ancestor_tx_logs: [ObjectId12];
```

`manifest.fbs` and `snapshot.fbs` are byte-identical to the previous milestone.
`common.fbs` and `transaction_log.fbs` changed **comments only** in the sense
that no table, field or type declaration moved — but the comments they changed
carry a **normative rule that this release reverses**: `common.fbs` now declares
node-path sort order to be the raw UTF-8 byte order of the whole path
(`/a < /a-b < /a/b`), where the previous milestone declared it component-wise
(`/a < /a/b < /a-b`); and `transaction_log.fbs` newly declares a *third*,
segment-wise order for `moved_nodes` and says explicitly that it is not the byte
order used for snapshot nodes. Read those two comment blocks rather than
assuming they are a restatement. Neither ordering is in scope at this milestone —
see `PROFILE.md` R13 and §11. No other table and no other field changed
anywhere.

`repo.fbs` also states the writer-side half of 2.0-reader compatibility: an
**empty `pruned_ancestor_tx_logs` vector is never written**, so an object this
release writes for a snapshot that was never expired is byte-shaped exactly like
one a 2.0 writer would have produced. It is a writer obligation with no
`icechunk::`-level observable — see §11.

Rust side, `icechunk::format::snapshot::SnapshotInfo`:

```rust
pub struct SnapshotInfo {
    pub id: SnapshotId,
    pub parent_id: Option<SnapshotId>,
    pub flushed_at: DateTime<Utc>,
    pub message: String,
    pub metadata: SnapshotProperties,          // BTreeMap<String, Value>
    pub pruned_ancestor_tx_logs: Vec<SnapshotId>,   // NEW
}
```

Semantics the verifier relies on:

* The list is **ordered by ancestry, oldest first**.
* It is **empty unless expiration edited this snapshot**. An object written
  without the field reads back as an empty `Vec`, i.e. "never expired". No
  migration, no error.
* It **accumulates**: when a snapshot that already carries a list is re-parented,
  the collapsed ancestor's own list is spliced in *before* that ancestor's id,
  and the re-parented snapshot's own existing list — newer than everything just
  collapsed — stays at the end. So the list grows monotonically across repeated
  expirations and stays in ancestry order. Both `expire` and `garbage_collect`
  populate it this way (§2.3, E1 and E9).
* The field lives on the repository table of contents (the `repo` object), not
  in the snapshot file. `SnapshotInfo::from_snapshot_file` therefore leaves it
  empty by construction; `RepoInfo::find_snapshot`, `Repository::lookup_snapshot`
  and `Repository::ancestry` return the populated value.
* A reader MUST NOT assume that each entry corresponds to a real commit, since
  an implementation MAY compact several entries into one
  (`spec.md` — `##### \`SnapshotInfo\``). The verifier never maps an entry to a
  commit and never asserts a length — but it does require the whole list to
  **equal** the transaction logs expiration removed, in order, so **do not
  compact at this milestone**. Compaction is a follow-up (`design/016`); a
  compacting implementation would fail these groups.

Reachable from outside through:

```rust
Repository::ancestry(&self, version: &VersionInfo)
    -> RepositoryResult<impl Stream<Item = RepositoryResult<SnapshotInfo>> + Send>
Repository::lookup_snapshot(&self, id: &SnapshotId) -> RepositoryResult<SnapshotInfo>
RepoInfo::find_snapshot(&self, id: &SnapshotId) -> Result<SnapshotInfo, IcechunkFormatError>
```

`ancestry` yields **newest first**, as it did at the previous milestone: the
first item is the snapshot the `VersionInfo` names, the second is its parent,
and the last is the initial snapshot. The verifier indexes that stream by
position, so the order is part of the contract rather than an incidental
detail.

### 2.2 There is no new spec-version constant

`icechunk::format::format_constants::SpecVersionBin` is still exactly

```rust
#[repr(u8)]
pub enum SpecVersionBin { V1 = 1u8, #[default] V2 = 2u8 }
impl SpecVersionBin { pub fn current() -> Self }        // == V2
```

`SpecVersionBin::try_from(3u8)` is
`Err(IcechunkFormatErrorKind::InvalidSpecVersion { found: 3, max_supported: 2 })`.

The on-disk header byte is `2` for spec 2.0 **and** spec 2.1
(`spec.md` — `### Binary File Format`; `version-policy.md` — `## On-Disk
Format`). **Nothing can tell 2.0 from 2.1 by looking at the header**, and the
verifier does not ask it to. Compatibility is checked as the observable property
the documents state: an object written with the field present is read correctly
by this release, and an object written without it is read correctly by this
release, both reporting header spec version `V2`.

`ICECHUNK_LIB_VERSION` is `env!("CARGO_PKG_VERSION")` and
`ICECHUNK_CLIENT_NAME` is `"ic-" + ICECHUNK_LIB_VERSION`. The verifier asserts
that a written header's `app_name` is `"ic"` and that `app_version` is present;
it does not pin the version text.

### 2.3 What expiration must do

`icechunk::ops::gc` (signatures unchanged from the previous milestone):

```rust
pub async fn expire(
    asset_manager: Arc<AssetManager>,
    older_than: DateTime<Utc>,
    expired_branches: ExpiredRefAction,
    expired_tags: ExpiredRefAction,
    repo_update_retries: Option<&RepoUpdateRetryConfig>,
    num_updates_per_repo_info_file: u16,
) -> GCResult<ExpireResult>;

pub async fn garbage_collect(
    asset_manager: Arc<AssetManager>,
    config: &GCConfig,
    repo_update_retries: Option<&RepoUpdateRetryConfig>,
    num_updates_per_repo_info_file: u16,
) -> GCResult<GCSummary>;

pub struct ExpireResult {
    pub released_snapshots: HashSet<SnapshotId>,
    pub edited_snapshots: HashSet<SnapshotId>,
    pub deleted_refs: HashSet<Ref>,
}
```

Behaviour in scope at this milestone:

| # | Requirement | Source |
| --- | --- | --- |
| E1 | Expiration writes the released ancestors' transaction-log ids onto the surviving boundary snapshot, oldest first. | `spec.md` — `##### \`SnapshotInfo\``; `design/016` — `### Population at expiration time` |
| E2 | `garbage_collect` never deletes a transaction log referenced by any snapshot's `pruned_ancestor_tx_logs`; the corresponding *snapshot files* stay deletable. A released snapshot with no surviving descendant has its log deleted. | `design/016` — `### GC retention rule` |
| E3 | `Repository::diff` concatenates each snapshot's pruned logs, oldest first, then the snapshot's own log, so a diff across an expiration boundary is complete. | `spec.md` — `##### \`SnapshotInfo\``; `design/016` — `#### \`Repository::diff\`` |
| E4 | An **amend** (`session.commit(msg).amend()`) copies the parent snapshot's `pruned_ancestor_tx_logs` onto the amended snapshot. A plain new commit records an empty list. | `design/016` — `#### Amend` |
| E5 | `Session::rebase` replays the pruned logs before the commit's own log, so a conflict recorded only in a pruned ancestor is still detected. | `design/016` — `#### \`Session::rebase\`` |
| E6 | With a referenced log **missing**, `diff` and `inspect` warn and skip, while `rebase` **errors** with `SessionErrorKind::MissingPrunedAncestorTxLog { snapshot, tx_log }`. | `design/016` — `## Compatibility` |
| E7 | `inspect::transaction_log_json` returns a merged, flagged composite whenever the list is non-empty (§6). | `design/016` — `#### \`inspect\`` |
| E8 | `older_than` is an exclusive bound: a snapshot is expired only if it was written strictly before the cutoff. | `guide/understanding-expiration.md` — `## Expire snapshots`, note "The cutoff is exclusive" |
| E9 | **`garbage_collect` records a pruned ancestry by the same rule `expire` does.** When GC deletes a snapshot that a *retained* snapshot still names as its parent — the interior gap `Repository::reset_branch` leaves when a branch is moved back over commits that are then collected — the retained snapshot is re-parented to its **nearest retained ancestor**, and every ancestor collapsed on the way is harvested into its `pruned_ancestor_tx_logs` by E1's rule (each collapsed ancestor's own list, then that ancestor's id, oldest first; the re-parented snapshot's own existing list stays newest and therefore last). When no ancestor survives, the new parent is the initial snapshot. | `design/016` — `### Population at expiration time`, whose rule §10 behaviour change 7 extends to `garbage_collect`; `design/016` — `### GC retention rule` |

E9 worked through, because it is the one obligation on `garbage_collect` that
is not simply retention. Commit `/a`, `/b`, `/c` on `main`, then
`reset_branch("main", &a, None)` and garbage-collect with a cutoff after `/b`:
`/b` is unreachable and is collected, but `/c` is still a known snapshot whose
parent is `/b`. GC must not leave `/c` dangling and must not simply re-root it,
because the delta from `/a` to `/c` includes `/b`'s changes. So `/c`'s parent
becomes `/a` — the nearest ancestor GC is keeping — and `/c`'s
`pruned_ancestor_tx_logs` becomes `[b]`. A later `expire` past `/a` then
prepends `a`, giving `[a, b]`: the same accumulation §2.1 describes, because
both operations use the same rule.

New error variant (`SessionErrorKind` is `#[non_exhaustive]`, so this is
additive):

```rust
MissingPrunedAncestorTxLog { snapshot: SnapshotId, tx_log: SnapshotId }
```

---

## 3. Header inspection without decoding the body

`icechunk::format::format_constants` (re-exported from `icechunk-format`):

```rust
pub const ICECHUNK_FORMAT_MAGIC_BYTES: &[u8];          // "ICE🧊CHUNK", 12 bytes
pub const ICECHUNK_IMPL_NAME_LEN: usize = 24;
pub const ICECHUNK_SPEC_VERSION_OFFSET: usize = 36;    // magic.len() + IMPL_NAME_LEN
pub const ICECHUNK_FILE_TYPE_OFFSET: usize = 37;
pub const ICECHUNK_COMPRESSION_OFFSET: usize = 38;
pub const ICECHUNK_FILE_HEADER_LEN: usize = 39;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FileHeader {
    pub impl_name: String,
    pub app_name: String,
    pub app_version: Option<String>,
    pub spec_version: SpecVersionBin,
    pub file_type: FileTypeBin,
    pub compression: CompressionAlgorithmBin,
}

pub fn parse_file_header(buf: &[u8]) -> Result<FileHeader, IcechunkFormatError>;
```

The layout is the one tabulated in `spec.md` — `### Binary File Format`
(`FileTypeBin::{Snapshot = 1, Manifest = 2, TransactionLog = 4, RepoInfo = 6}`;
`CompressionAlgorithmBin::{None = 0, Zstd = 1}`; both `#[repr(u8)]`, both
`PartialEq` so a parsed header can be compared against a variant).

`parse_file_header` semantics:

* It **discovers** rather than asserts the file type, so it works for any
  metadata file kind.
* Bytes after `ICECHUNK_FILE_HEADER_LEN` are ignored.
* The 24-byte implementation field is trimmed of trailing `' '` and `'\0'`;
  `app_name`/`app_version` are a best-effort `impl_name.split_once('-')`, with
  `app_version == None` when there is no `-`.
* Error categories (`IcechunkFormatErrorKind` is `#[non_exhaustive]`):

  | Input | Kind |
  | --- | --- |
  | fewer than 39 bytes | `InvalidIcechunkHeaderSize { found, expected }` (**new**) |
  | wrong magic | `InvalidMagicNumbers` |
  | unknown file-type code | `UnknownFileType { got }` (**new**) |
  | unknown spec-version byte | `InvalidSpecVersion { found, max_supported }` |
  | unknown compression code | `InvalidCompressionAlgorithm` |

`icechunk::asset_manager::AssetManager`, reachable through
`Repository::asset_manager(&self) -> &Arc<AssetManager>`:

```rust
pub async fn fetch_header(&self, path: &str) -> RepositoryResult<FileHeader>;
pub async fn fetch_repo_info_header(&self) -> RepositoryResult<FileHeader>;
pub async fn fetch_repo_info_backup_header(&self, file_name: &str) -> RepositoryResult<FileHeader>;
pub async fn fetch_snapshot_header(&self, snapshot_id: &SnapshotId) -> RepositoryResult<FileHeader>;
pub async fn fetch_manifest_header(&self, manifest_id: &ManifestId) -> RepositoryResult<FileHeader>;
pub async fn fetch_transaction_log_header(&self, transaction_id: &SnapshotId) -> RepositoryResult<FileHeader>;

pub async fn fetch_snapshot_with_header(&self, snapshot_id: &SnapshotId)
    -> RepositoryResult<(Arc<Snapshot>, FileHeader)>;
pub async fn fetch_manifest_with_header(&self, manifest_id: &ManifestId, manifest_size: u64)
    -> RepositoryResult<(Arc<Manifest>, FileHeader)>;
pub async fn fetch_transaction_log_with_header(&self, transaction_id: &SnapshotId)
    -> RepositoryResult<(Arc<TransactionLog>, FileHeader)>;
pub async fn fetch_repo_info_with_header(&self)
    -> RepositoryResult<(Arc<RepoInfo>, storage::VersionInfo, FileHeader)>;
pub async fn fetch_repo_info_backup_with_header(&self, file_name: &str)
    -> RepositoryResult<(Arc<RepoInfo>, storage::VersionInfo, FileHeader)>;

pub fn clear_manifest_cache(&self);
pub fn clear_snapshot_cache(&self);
```

Contract the verifier checks:

* `fetch_header` issues a **range GET of exactly `0..ICECHUNK_FILE_HEADER_LEN`**
  and never downloads or decompresses the body. The verifier observes this
  without counting requests: it truncates a stored metadata object to exactly
  its 39-byte header, and then the header read must still succeed while the
  full decode must fail.
* The per-type wrappers only build the storage path
  (`repo`, `snapshots/<id>`, `manifests/<id>`, `transactions/<id>`,
  `overwritten/<file_name>`); `fetch_header` itself reports whatever type it
  finds at the path it is given.
* A header for a path that holds no object is an error, not a default value.
* The `*_with_header` variants decode the object, bypass the value cache, and
  return the same `FileHeader` the header-only call returns.

---

## 4. Static HTTP headers

`icechunk::config` (`#[cfg(feature = "object-store-http")]`, on by default):

```rust
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq, Default)]
pub struct HttpConfig {
    #[serde(default, flatten)]
    pub opts: HashMap<String, String>,      // ClientConfigKey names, snake_case
    #[serde(default, skip_serializing_if = "HashMap::is_empty")]
    pub headers: HashMap<String, String>,   // injected into every request
}

pub enum ObjectStoreConfig {
    /* … */
    #[cfg(feature = "object-store-http")]
    Http(HttpConfig),                       // was Http(HashMap<String, String>)
    /* … */
}
```

The `#[serde(flatten)]` on `opts` is load-bearing: a document written by the
previous milestone, which serialized the variant as a flat map of transport
options (`{"http":{"allow_http":"true"}}` in JSON, `!http\n  allow_http: "true"`
in YAML), must still deserialize, yielding those entries in `opts` and an empty
`headers`.

Such a document is normally a whole `RepositoryConfig`, and the verifier loads
one, so the surrounding spelling is part of the same compatibility obligation.
`ObjectStoreConfig` is `#[serde(rename_all = "snake_case")]`, which is what makes
the YAML tag `!http`. `RepositoryConfig` and `VirtualChunkContainer` carry no
rename attribute at all, so every field is spelled with its Rust name:
`RepositoryConfig::virtual_chunk_containers` is an optional map keyed by url
prefix, `RepositoryConfig::manifest` is one of its other optional fields, and a
`VirtualChunkContainer` value inside that map is
`{ name (nullable), url_prefix, store }` — `store` holding the
`ObjectStoreConfig` payload above.

Storage plumbing, `icechunk::storage`:

```rust
#[cfg(feature = "object-store-http")]
pub fn new_http_storage(
    base_url: &str,
    config: Option<HashMap<String, String>>,
    headers: Option<HashMap<String, String>>,        // NEW third parameter
) -> StorageResult<Arc<dyn Storage + Send + Sync>>;

#[cfg(feature = "object-store-http")]
impl ObjectStorage {
    pub fn new_http(
        url: &Url,
        config: Option<HashMap<ClientConfigKey, String>>,
        headers: Option<HashMap<String, String>>,    // NEW third parameter
    ) -> Result<ObjectStorage, StorageError>;
}

#[cfg(feature = "object-store-http")]
pub struct HttpObjectStoreBackend {
    pub url: String,
    pub config: Option<HashMap<ClientConfigKey, String>>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub headers: Option<HashMap<String, String>>,    // NEW public field
}
```

`ObjectStoreBackend` is re-exported at `icechunk::storage::ObjectStoreBackend`;
its `mk_object_store(&self, settings: &Settings) -> Result<Arc<dyn ObjectStore>, StorageError>`
is where headers are installed. The verifier checks the **configuration
surface**, never a network request:

* `mk_object_store` is `Ok` for a valid header map, for options alone, for both
  together, and for neither. Headers and transport options must coexist — an
  implementation that installs one by replacing the other is wrong — but the
  merged client options live inside `object_store` and are not readable at any
  `icechunk::` path, so what is *in scope* is only that constructing with both
  succeeds; see §11.
* `mk_object_store` is `Err` when a header cannot be represented on the wire —
  a name containing a space, or a value containing a newline.

Virtual-chunk side, `icechunk::virtual_chunks::ObjectStoreFetcher::new_http`
gained `headers: &HashMap<String, String>` and `settings: storage::Settings`.

---

## 5. Explicit no-credential sentinels

`icechunk::config::Credentials` (**not** `#[non_exhaustive]`; an exhaustive
match on it now needs two more arms):

```rust
pub enum Credentials {
    S3(S3Credentials),
    #[cfg(feature = "object-store-gcs")]   Gcs(GcsCredentials),
    #[cfg(feature = "object-store-azure")] Azure(AzureCredentials),
    LocalFileSystemAccess,   // NEW
    HttpAccess,              // NEW
}
```

`VirtualChunkContainer::validate_credentials(&self, cred: Option<&Credentials>)
-> Result<(), VirtualChunkCredentialsError>` (signature unchanged) accepts:

| container backend | accepted | rejected |
| --- | --- | --- |
| `ObjectStoreConfig::LocalFileSystem` | `Some(LocalFileSystemAccess)`, `None` | any other `Some(_)` |
| `ObjectStoreConfig::Http` | `Some(HttpAccess)`, `None` | any other `Some(_)` |
| `ObjectStoreConfig::S3` / `S3Compatible` / `Tigris` | `Some(S3(_))`, `None` | the two new sentinels |

`None` still validates — it is deprecated, not removed
(`reference/changelog.md`, `### Deprecations`). `Repository::create` and
`Repository::open` additionally emit a deprecation `warn!` for a `None` value on
an `s3`/`tigris`/`gcs`/`gs`/`file`/`http`/`https` container. **The log line is
out of scope**: a `warn!` with no subscriber installed is unobservable, and the
verifier neither installs one nor asserts any text.

The sentinel also has to work end to end: passing
`HashMap::from([(prefix, Some(Credentials::LocalFileSystemAccess))])` as
`authorize_virtual_chunk_access` to `Repository::create` must authorize that
`file://` container, so the prefix appears in
`Repository::authorized_virtual_container_prefixes()` and a `file://` virtual
chunk under it reads back.

Also new and required to exist:
`icechunk::config::AzureCredentials::Anonymous`.

---

## 6. Inspecting a composite transaction log

`icechunk::inspect::transaction_log_json(asset_manager: &AssetManager,
id: &SnapshotId, pretty: bool) -> RepositoryResult<String>` keeps its signature
and changes behaviour. When the snapshot's `pruned_ancestor_tx_logs` is
non-empty the returned JSON is a **merged composite** of the pruned logs
(oldest first) followed by the snapshot's own log, and carries an object under
the key

```
synthetic_composite
```

with, at least, the string-array members

```
merged_pruned_ancestor_tx_logs    ids of the pruned logs that were merged, oldest first
missing_tx_logs                   ids that were referenced but could not be read
```

When the list is empty the JSON is the plain transaction-log record and has
**no** `synthetic_composite` key. A referenced log that cannot be read is listed
in `missing_tx_logs` and its content is dropped from the merge; the surviving
logs are still merged.

"Merged" is about **content**, not only about the three key names. The composite
carries the merged logs' own change-set entries, spelled exactly as
`transaction_log_json` spells them when it renders one of those logs on its own:
a node that appears in a merged log's plain rendering appears in the composite,
and a node that appears only in a log listed under `missing_tx_logs` does not.
The rendering itself is inherited unchanged from the previous milestone; the
requirement here is the equality, not a redesign of the document. One detail of
that inherited rendering the verifier does depend on, so it is pinned rather
than left open: **every change-set member that names a node spells it as that
node's `NodeId` in its `Display` form** — the same uppercase Crockford Base32
spelling §9.2 gives for `SnapshotId` — and not as the node's path. That covers
`new_groups`, `new_arrays`, `deleted_groups`, `deleted_arrays`,
`updated_groups`, `updated_arrays` and the `node_id` of each `updated_chunks`
entry. (`moved_nodes` is the exception, and names paths, not ids.)

The three key names, that equality and that spelling are the in-scope part of the
JSON contract; sibling keys, field order and whitespace are out of scope.

---

## 7. Creating a repository at an object store's root

`icechunk::storage` (re-exported from `icechunk-storage`):

```rust
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RepositoryCreation { Allowed, RefusedEmptyPrefix }

pub trait Storage /* sealed */ {
    async fn can_create_repository(&self) -> StorageResult<RepositoryCreation> { Ok(Allowed) }
    async fn create_location_if_needed(&self) -> StorageResult<()> { Ok(()) }
}
```

`RepositoryErrorKind` (`#[non_exhaustive]`) gains `EmptyPrefixCreation`, and
`Repository::create` gates on it **before any I/O**:

```rust
if storage.can_create_repository().await? == RepositoryCreation::RefusedEmptyPrefix {
    return Err(RepositoryErrorKind::EmptyPrefixCreation.into());
}
storage.create_location_if_needed().await?;
```

Behaviour in scope (`guide/guides-storage.md` —
`### Empty prefix and legacy repositories`):

* A cloud object-store handle addressed at the bucket root (empty prefix)
  reports `RefusedEmptyPrefix`, and `Repository::create` on it returns
  `RepositoryErrorKind::EmptyPrefixCreation`. Because the gate runs first, this
  is observable with **no live endpoint**.
* A non-empty prefix, in-memory storage and local-filesystem storage all report
  `Allowed`.
* The escape hatches re-allow the root and consume `self`:
  `ObjectStorage::unsafe_allow_empty_prefix_creation(self) -> Self` and
  `S3Storage::unsafe_allow_empty_prefix_creation(self) -> Self`. The two handles
  those methods belong to are built without I/O by
  `s3_storage(config, bucket, prefix, credentials, legacy_rooted_keys)
  -> StorageResult<S3Storage>` (§10) and
  `async ObjectStorage::new_s3(bucket: String, prefix: Option<String>,
  credentials: Option<S3Credentials>, config: Option<S3Options>)
  -> Result<ObjectStorage, StorageError>` (it is `async`, and performs no I/O);
  a `None`/empty prefix is the bucket root for both.
* `Repository::open` never consults `can_create_repository`, so an existing
  empty-prefix repository still opens. `Repository::open` on local-filesystem
  storage no longer creates the directory; `Repository::create` does — through
  `create_location_if_needed`, which is a no-op for a backend with nothing to
  create and `mkdir -p` for the local filesystem.
* `Storage::root_is_clean(&self, settings: &Settings) -> StorageResult<bool>`
  reports a location that does **not exist yet** as clean rather than
  propagating `StorageErrorKind::ObjectNotFound` (§10 behaviour change 6), which
  is what lets `create` with `check_clean_root = true` succeed on a fresh
  directory.
* The backend-level half of the same gate is
  `ObjectStoreBackend::restricts_empty_prefix_creation(&self) -> bool`, a new
  provided method (§10 shape table). `ObjectStorage::can_create_repository`
  reports `RefusedEmptyPrefix` only when the backend's `prefix()` is empty, the
  escape hatch has not been used **and** the backend reports `true` here. The
  S3 object-store backend reports `true` — which is what makes
  `ObjectStorage::new_s3` at a bucket root `RefusedEmptyPrefix` above — while
  the in-memory, local-filesystem and HTTP backends keep the provided `false`,
  which is why an `ObjectStorage` over those three is always `Allowed` even
  though their `prefix()` is empty (§9.0).

The `legacy_rooted_keys` / `KeyLayout` **read** path for pre-existing rooted
native-S3 repositories is out of scope — see §11.

---

## 8. Virtual chunk locations

`icechunk::format::manifest`:

```rust
pub const VCC_RELATIVE_URL_SCHEME: &str = "vcc://";
pub struct VirtualChunkLocation(String);
impl VirtualChunkLocation {
    pub fn url(&self) -> &str;
    pub fn is_relative(&self) -> bool;                 // starts with "vcc://"
    pub fn from_url(path: &str) -> Result<VirtualChunkLocation, VirtualReferenceError>;
}
```

`from_url` dispatches on the scheme:

* **Any scheme other than `file`** (including `vcc`): the string is stored
  **verbatim**, with only the scheme ASCII-lowercased. Userinfo, port, query,
  fragment, repeated `//`, `.` and `..` all survive unchanged. The authority —
  everything up to the first `/`, `?` or `#` — must be non-empty, otherwise
  `CannotParseBucketName` (or `NoContainerForName` for `vcc://`).
* **`file`**, and anything without a clean `scheme://`: WHATWG normalization, so
  `..` and `%2e%2e` are resolved away (this is the container-confinement
  boundary) and `file:/x` becomes `file:///x`.

Error categories (`VirtualReferenceErrorKind`, `#[non_exhaustive]`):

| input | kind |
| --- | --- |
| `"not-a-url"`, `""` | `CannotParseUrl { cause, url }` |
| `"mailto:foo"` | `NoPathSegments(_)` |
| `"s3:///key"`, `"s3:/bucket/key"` | `CannotParseBucketName(_)` |
| `"vcc:///rel"` | `NoContainerForName(_)` |
| a key with `//`, `.` or `..` segments on an `object_store`-backed container | `UnsupportedObjectKeyForBackend(_)` (**new**) |

Resolution entry points:

```rust
impl VirtualChunkResolver {
    pub fn validate_virtual_chunk_location(&self, location: &VirtualChunkLocation)
        -> Result<(), VirtualReferenceError>;
}
impl Session {
    pub fn matching_container(&self, location: &VirtualChunkLocation) -> Option<&VirtualChunkContainer>;
    pub fn validate_virtual_chunk_location(&self, location: &VirtualChunkLocation)
        -> Result<(), VirtualReferenceError>;
    pub fn resolve_virtual_location(&self, location: &VirtualChunkLocation)
        -> Result<String, VirtualReferenceError>;   // expands vcc://, passes absolute URLs through
}
```

`resolve_virtual_location` distinguishes the two "no container" categories:
a `vcc://<name>/...` reference whose `<name>` matches no container is
`NoContainerForName`, while an **absolute** location that no container's url
prefix covers is `NoContainerForUrl` (§9.5). An absolute location that no
container covers is returned unchanged by `resolve_virtual_location` and refused
by `validate_virtual_chunk_location`.

**Port honouring.** The port is part of the location, part of the container's
url prefix, and part of the key that distinguishes one HTTP fetcher from
another. The verifier checks that offline: a location keeps its non-default
port, two containers that differ only in port match different locations and a
third port matches neither, and `resolve_virtual_location` expands a
`vcc://name/...` reference against its container's prefix *including the port*.
A **default** port is not special: `from_url` keeps `:80` and `:443` exactly as
written, like any other part of a non-`file` location, because the whole string
is copied verbatim. (A URL parser drops a default port, and this release does
parse a location when it derives the host key a virtual-chunk fetcher is cached
under — but that derivation is internal and is out of scope.) Actually fetching
bytes over HTTP is out of scope — see §11.

`Store::set_virtual_ref(&self, key: &str, reference: VirtualChunkRef,
validate_container: bool)` with `validate_container = true` must surface an
unrepresentable key as
`StoreErrorKind::SessionError(SessionErrorKind::VirtualReferenceError(
VirtualReferenceErrorKind::UnsupportedObjectKeyForBackend(_)))`.

Related helper added at this milestone:
`Path::parent(&self) -> Option<Path>`, on `icechunk::format::Path`.

---

## 9. The inherited contract, as this release spells it

A later step checks every earlier milestone again. What follows is the shape those
groups drive; it is the current release's spelling, not an earlier one's.

### 9.0 Inherited symbols the groups construct with

§§9.1-9.6 describe inherited *behaviour*. The list below is the inherited
*plumbing* the same groups compile against: names that already exist in the
workspace you are handed, whose signatures this release does not change, and
which therefore need no work — but which must keep resolving at these
`icechunk::` paths, because a driver that cannot name them cannot reach the
behaviour above. Preservation rule 1 in `WORKSPACE_HANDOFF.md` already covers
them; they are enumerated here so "is my inherited surface still sufficient?"
can be answered from this document alone.

**Paths and `async`.** Every entry below is written at the full `icechunk::`
path the driver imports it from, and `async` is written where the function is
`async`. The crate root re-exports `ObjectStorage`, `ObjectStoreConfig`,
`Repository`, `Storage`, `Store`, `new_in_memory_storage`,
`new_local_filesystem_storage` and `new_s3_storage`; everything else is reached
at its module path (`icechunk::config::`, `icechunk::error::`,
`icechunk::format::`, `icechunk::refs::`, `icechunk::repository::`,
`icechunk::session::`, `icechunk::storage::`, `icechunk::store::`,
`icechunk::virtual_chunks::`, `icechunk::inspect::`, `icechunk::ops::gc::`).

```rust
// storage constructors — every case builds its repository with one of these
async icechunk::new_in_memory_storage() -> StorageResult<Arc<dyn Storage + Send + Sync>>
async icechunk::new_local_filesystem_storage(path: &std::path::Path)
    -> StorageResult<Arc<dyn Storage + Send + Sync>>
icechunk::storage::Storage::{can_write, default_settings}   // async trait methods
// `can_write` reports whether the backend accepts writes at all. It is `false`
// for the HTTP backend, which is a read-only transport
// (`guide/guides-storage.md` — `### HTTP Storage`), and `true` for the
// in-memory, local-filesystem and object-store S3 backends; a native
// `S3Storage` reports the `can_write` flag it was constructed with (§10).
// This is inherited behaviour, unchanged at this milestone.
icechunk::storage::Settings: Default                        // `Settings::default()`
icechunk::storage::S3Storage                                // the native-S3 handle
icechunk::error::ICError { kind, .. }                       // every error destructures this way
icechunk::format::IcechunkFormatErrorKind
icechunk::refs::RefErrorKind
icechunk::store::StoreErrorKind
// `prefix` is the key prefix a backend prepends to every object path. The
// in-memory, local-filesystem and HTTP backends keep the whole location in
// their own field, so their `prefix()` is the empty string; the S3 backends
// return the prefix they were built with.
icechunk::storage::ObjectStoreBackend::prefix(&self) -> String

// asset manager, beyond the header wrappers of §3
AssetManager::fetch_snapshot(&self, id: &SnapshotId) -> RepositoryResult<Arc<Snapshot>>
AssetManager::fetch_transaction_log(&self, id: &SnapshotId)
    -> RepositoryResult<Arc<TransactionLog>>
AssetManager::remove_cached_tx_log(&self, id: &SnapshotId)     // the third cache method
AssetManager::delete_transaction_logs(&self, logs: BoxStream<'_, (SnapshotId, u64)>)
    -> RepositoryResult<DeleteObjectsResult>

// expiration and GC parameter types
icechunk::ops::gc::ExpiredRefAction::{Ignore, Delete}
GCConfig::clean_all(chunks_age: DateTime<Utc>, metadata_age: DateTime<Utc>,
                    extra_roots: Option<HashSet<SnapshotId>>,
                    max_snapshots_in_memory: NonZeroU16,
                    max_compressed_manifest_mem_bytes: NonZeroUsize,
                    max_concurrent_manifest_fetches: NonZeroU16,
                    dry_run: bool) -> GCConfig

// repository and session accessors
SnapshotInfo::is_initial(&self) -> bool
SnapshotId::random() -> SnapshotId
Session::snapshot_id(&self) -> &SnapshotId
Session::read_only(&self) -> bool
DetectedSpecVersion::spec_version(&self) -> SpecVersionBin
Diff::new_groups:     BTreeSet<Path>                           // public field
Diff::new_arrays:     BTreeSet<Path>                           // public field
Diff::updated_chunks: BTreeMap<Path, BTreeSet<ChunkIndices>>   // public field
ManifestFileInfo::{id, size_bytes}                             // public fields
NodeSnapshot::{id, path}                                       // public fields, via get_node/get_array/get_group
icechunk::conflicts::detector::ConflictDetector                // the concrete ConflictSolver

// format and chunk plumbing
icechunk::format::Path::{root, try_from, parent}
icechunk::format::ByteRange::ALL
icechunk::format::ChunkIndices
icechunk::format::manifest::ChunkPayload::{Inline, Virtual}
icechunk::format::manifest::VirtualChunkRef { location, offset, length, checksum }
icechunk::format::snapshot::DimensionName::from(Option<&str>)

// configuration
RepositoryConfig::set_virtual_chunk_container(&mut self, c: VirtualChunkContainer)
    -> Result<(), String>
RepositoryConfig::virtual_chunk_containers: Option<HashMap<String, VirtualChunkContainer>>
RepositoryConfig::num_updates_per_repo_info_file: Option<u16>  // default 1000; after that
    // many updates the previous `repo` object is rotated to `overwritten/<name>`,
    // and `RepoInfo::repo_before_updates()` names the most recent one (§9.6)
icechunk::config::ObjectStoreConfig::{InMemory, LocalFileSystem(PathBuf),
    S3(S3Options), S3Compatible(S3Options), Tigris(S3Options)}  // payloads
VirtualChunkContainer::url_prefix(&self) -> &str               // accessor, not only a ctor arg
VirtualChunkContainer::store: ObjectStoreConfig                // public field
VirtualChunkResolver::new(containers: impl Iterator<Item = VirtualChunkContainer>,
                          credentials: HashMap<String, Option<Credentials>>,
                          settings: storage::Settings) -> VirtualChunkResolver
icechunk::config::S3Credentials::Static(S3StaticCredentials {
    access_key_id, secret_access_key, session_token, expires_after })
```

Nothing in §9.0 is new work and nothing in it is a behaviour requirement: if
these still resolve and still mean what they meant at the previous milestone,
the section is satisfied.

### 9.1 Repository, session, refs (milestones 01-02)

Every `Repository` method below is **`async`** except the two pure accessors
`spec_version(&self) -> SpecVersionBin` and
`asset_manager(&self) -> &Arc<AssetManager>` (and
`authorized_virtual_container_prefixes`, §5). The `async` keyword is elided in
the block for width; read every `-> RepositoryResult<…>` as
`-> impl Future<Output = RepositoryResult<…>>`. The same holds for `Session`'s
read and write methods, `CommitBuilder::execute`, `Session::rebase`, every
`Storage` trait method, and every `AssetManager` `fetch_*` and
`delete_transaction_logs`. The synchronous exceptions are
`AssetManager::{clear_manifest_cache, clear_snapshot_cache,
remove_cached_tx_log}` (§3, §9.0),
`ObjectStoreBackend::{mk_object_store, prefix}` (§4, §9.0),
`Session::{snapshot_id, read_only, matching_container,
validate_virtual_chunk_location, resolve_virtual_location}` and
`Session::commit`, which returns the builder.

```rust
Repository::create(config: Option<RepositoryConfig>,
                   storage: Arc<dyn Storage + Send + Sync>,
                   authorize_virtual_chunk_access: HashMap<String, Option<Credentials>>,
                   spec_version: Option<SpecVersionBin>,
                   check_clean_root: bool) -> RepositoryResult<Repository>;
Repository::open(config, storage, authorize_virtual_chunk_access) -> RepositoryResult<Repository>;
Repository::exists(storage, settings: Option<storage::Settings>) -> RepositoryResult<bool>;
Repository::fetch_spec_version(storage, settings) -> RepositoryResult<Option<DetectedSpecVersion>>;
Repository::spec_version(&self) -> SpecVersionBin;
Repository::{writable_session, rearrange_session}(&self, branch: &str) -> RepositoryResult<Session>;
Repository::readonly_session(&self, version: &VersionInfo) -> RepositoryResult<Session>;
Repository::{create_branch, create_tag}(&self, name: &str, id: &SnapshotId) -> RepositoryResult<()>;
Repository::{list_branches, list_tags}(&self) -> RepositoryResult<BTreeSet<String>>;
Repository::{lookup_branch, lookup_tag}(&self, name: &str) -> RepositoryResult<SnapshotId>;
Repository::reset_branch(&self, branch: &str, to: &SnapshotId, from: Option<&SnapshotId>) -> RepositoryResult<()>;
Repository::diff(&self, from: &VersionInfo, to: &VersionInfo) -> SessionResult<Diff>;
Repository::lookup_manifest_files(&self, id: &SnapshotId)
    -> RepositoryResult<impl Iterator<Item = ManifestFileInfo>>;
```

`VersionInfo` is `#[non_exhaustive]` with `SnapshotId`, `TagRef`, `BranchTipRef`
and `AsOf { branch, at }`.

`Session` writes through `add_group`, `add_array`, `update_array`,
`update_group`, `delete_*`, `set_chunk_ref`, `move_node`; reads through
`get_node`, `get_array`, `get_group`, `list_nodes`, `all_chunks`; and commits
through the builder

```rust
Session::commit(&mut self, message: impl Into<String>) -> CommitBuilder<'_>;
CommitBuilder::{properties, max_concurrent_nodes, allow_empty, amend, anonymous,
                rewrite_manifests, rebase, before_rebase_hook, after_rebase_hook}
CommitBuilder::execute(self) -> SessionResult<SnapshotId>;
Session::rebase(&mut self, solver: &dyn ConflictSolver) -> SessionResult<()>;
```

Error categories a regression test observes, all reached through
`ICError { kind, .. }`:

* a write on a read-only session → `SessionErrorKind::ReadOnlySession`;
* a losing concurrent commit → `SessionErrorKind::Conflict { .. }`, and rebasing
  it over a real conflict → `SessionErrorKind::RebaseFailed { conflicts, .. }`
  carrying `conflicts::Conflict::NewNodeConflictsWithExistingNode(path)`;
* a missing node → `SessionErrorKind::NodeNotFound { .. }`;
* `Repository::lookup_snapshot` on an unknown id →
  `RepositoryErrorKind::FormatError(IcechunkFormatErrorKind::SnapshotIdNotFound { .. })`;
* `Repository::lookup_branch` on an unknown branch →
  `RepositoryErrorKind::Ref(RefErrorKind::RefNotFound(_))`;
* re-creating an existing tag is an error (tags are immutable and non-reusable).

`async Store::from_session(Arc<tokio::sync::RwLock<Session>>) -> Store` gives
the Zarr key interface: `get`, `set`, `exists`, `delete`, `list`, `list_prefix`, `list_dir`,
`set_virtual_ref`. Chunk keys use the array's `chunk_key_encoding` separator;
the one-dimensional arrays the verifier writes are addressed as
`"<array>/c/<i>"`.

`ArrayShape::new<I: IntoIterator<Item = (u64, u32)>>(it) -> Option<ArrayShape>`
takes **(array length, number of chunks)** per dimension — the second element is
a chunk *count*, not a chunk size.

### 9.2 Format, headers, expiration accounting (milestones 03-05)

Every written metadata object begins with the 39-byte header of §3; a snapshot
file is at `snapshots/<id>` and the table of contents at `repo`, relative to the
storage prefix. `GCSummary` reports
`{bytes_deleted, chunks_deleted, manifests_deleted, snapshots_deleted,
attributes_deleted, transaction_logs_deleted}`. A tag keeps its snapshot alive
across `expire` + `garbage_collect`.

`GCConfig::clean_all(chunks_age, metadata_age, extra_roots,
max_snapshots_in_memory, max_compressed_manifest_mem_bytes,
max_concurrent_manifest_fetches, dry_run)` is the constructor the verifier uses.

A decoded `icechunk::format::transaction_log::TransactionLog` exposes its change
sets as iterators of node ids: `new_groups()`, `new_arrays()`,
`deleted_groups()`, `deleted_arrays()`, `updated_groups()`, `updated_arrays()`,
`updated_chunks()`, `moved_nodes()`. `Snapshot` exposes `id()`, `parent_id()`,
`message()`, `flushed_at()`, `metadata()` and `manifest_files()`.
`SnapshotId` implements `Display` as uppercase Crockford Base32 with no padding
(`spec.md` — `### Common Types`), which is also its file name under
`snapshots/`.

### 9.3 The command-line interface (milestone 05)

Built with `--features cli`; the verifier finds the binary through the
environment variable **`ICECHUNK_TEST_CLI_BIN`**.

* `icechunk --help` exits **0** and names the three command groups `repo`,
  `snapshot` and `config`.
* `icechunk repo create <alias>` creates the repository named by `<alias>` in
  the config file and exits 0.
* `icechunk snapshot list <alias>` prints one line per snapshot on stdout
  (default branch `main`, default `-n 10`) and exits 0. The per-line rendering
  is **out of scope**, only that one line is produced per snapshot.
* An unknown subcommand, or an alias absent from the config, exits **non-zero**.
* The config file is `<config dir>/icechunk/cli-config.yaml`, where the config
  directory follows the platform convention (`$XDG_CONFIG_HOME`, else
  `$HOME/.config`, on Linux). Its schema is

  ```yaml
  repos:
    <alias>: !LocalFileSystem
      path: /some/directory
      config: {}
  ```

  `RepositoryDefinition` is a Rust enum serialized by `serde_yaml_ng`, so each
  variant is written as a **YAML tag** (`!LocalFileSystem`, `!S3`, `!Tigris`,
  `!Azure`, `!GCS`). `config` is a `RepositoryConfig`, and `{}` means defaults.

### 9.4 Manifest splitting (milestone 06)

```rust
RepositoryConfig { manifest: Option<ManifestConfig>, .. }
ManifestConfig { preload, splitting: Option<ManifestSplittingConfig>, virtual_chunk_location_compression }
ManifestSplittingConfig { pub split_sizes: Option<Vec<(ManifestSplitCondition, Vec<ManifestSplitDim>)>> }
ManifestSplittingConfig::with_size(split_size: u32) -> Self       // every array, every axis
ManifestSplitCondition::{Or, And, PathMatches { regex }, NameMatches { regex }, AnyArray}
// PathMatches tests the node's WHOLE path ("/split");
// NameMatches tests only its FINAL path segment ("split").
ManifestSplitDim { condition: ManifestSplitDimCondition, num_chunks: u32 }
ManifestSplitDimCondition::{Axis(usize), DimensionName(String), Any}
```

A split policy makes one array span several manifests
(`Repository::lookup_manifest_files` returns more than one `ManifestFileInfo`);
the default policy yields exactly one per array; and the policy never changes
what a read returns.

### 9.5 Virtual chunk containers and inspect (milestones 07-08)

`VirtualChunkContainer::new(url_prefix: String, store: ObjectStoreConfig)` and
`::new_named(name, url_prefix, store)` return `Err(String)` unless the prefix
**ends in `/`** and its scheme matches the backend. Prefix matching is literal,
so `s3://bucket/authorized/` does not cover `s3://bucket/authorized-evil/`.
A location no container covers is `VirtualReferenceErrorKind::NoContainerForUrl`.
`icechunk::inspect::{snapshot_json, repo_info_json, manifest_json,
transaction_log_json}` each return a JSON document; `snapshot_json` carries the
snapshot id under the key `id`.

### 9.6 The Icechunk 2 table of contents and ops log (milestone 09)

`async AssetManager::fetch_repo_info(&self) -> RepositoryResult<(Arc<RepoInfo>, storage::VersionInfo)>`
returns the single mutable `repo` object. `RepoInfo` exposes
`spec_version()`, `branches()`, `branch_names()`, `tags()`, `tag_names()`,
`resolve_branch()`, `resolve_tag()`, `find_snapshot()`, `all_snapshots()`,
`latest_updates()` and `repo_before_updates()`. They are synchronous and
fallible: `branches()` is
`IcechunkResult<impl Iterator<Item = (&str, SnapshotId)>>` and
`repo_before_updates()` is `IcechunkResult<Option<&str>>`, naming the rotated
`overwritten/` file if there is one. The name it yields is the **bare file
name**, without the `overwritten/` prefix — it is what
`AssetManager::fetch_repo_info_backup_header` and
`fetch_repo_info_backup_with_header` take (§3), and those prepend the prefix
themselves.

```rust
async Repository::ops_log(&self) -> RepositoryResult<(
    impl Stream<Item = RepositoryResult<(DateTime<Utc>, UpdateType, Option<String>)>> + Send,
    Arc<RepoInfo>,
    storage::VersionInfo)>;
```

`icechunk::format::repo_info::UpdateType` includes at least
`RepoInitializedUpdate`, `BranchCreatedUpdate { name }`,
`TagCreatedUpdate { name }`, `BranchDeletedUpdate { .. }`,
`TagDeletedUpdate { .. }`, `ConfigChangedUpdate`, `MetadataChangedUpdate`,
`RepoStatusChangedUpdate { .. }`, `RepoMigratedUpdate { .. }`.

Feature flags — all three are `async` and fallible:
`async Repository::{feature_flags, enabled_feature_flags,
disabled_feature_flags}(&self) -> RepositoryResult<impl Iterator<Item = FeatureFlag>>`
(`FeatureFlag` has `id()`, `name()`, `default_enabled()`,
`default_disabled()`, `setting() -> Option<bool>`, `in_default_state()`,
`enabled()`), and
`async Repository::set_feature_flag(&self, feature_flag: &str,
set_to: Option<bool>) -> RepositoryResult<()>` enables, disables or resets one.
`"move_node"` is one of the documented flag names.

---

## 10. Migration from the previous milestone

Every item in the tables below is source-breaking for code written against the
previous milestone. The verifier drives the **replacement**. The last
subsection is different in kind: nothing about the behaviour changed, only
whether this milestone's groups **check** it.

### Removed

| removed | replacement |
| --- | --- |
| `impl TryFrom<&Snapshot> for SnapshotInfo` | `SnapshotInfo::from_snapshot_file(&Snapshot) -> Result<SnapshotInfo, IcechunkFormatError>` |
| `S3Storage::get_path_str(&self, file_prefix: &str, id: &str)` | none; key joining is internal and layout-aware |

Upstream also split its single `reference/spec.md` into `reference/spec-v2-1.md`
(the current specification, and the same document as the corpus' top-level
`spec.md`) and `reference/spec-v2.md` (spec 2.0, frozen). That is a change to
*their* documentation tree, not to this crate, and the milestone's corpus is
overlaid rather than replaced, so the **previous** milestone's
`/app/icechunk-spec/reference/spec.md` — which presents spec 2.0 as current — is
still on disk beside the two new files. It is superseded and non-normative; see
`/app/icechunk-spec/README.md` and `PROFILE.md` R12.

### Signature changes — one new trailing parameter each

Module paths and the argument types that are not obvious from the names:
`icechunk::new_s3_storage` is re-exported at the crate root;
`icechunk::storage::{s3_storage, new_r2_storage, new_tigris_storage,
new_http_storage, S3Storage}` are not. `new_r2_storage` takes
`bucket: Option<String>` and `account_id: Option<String>`; every `config` below
is an `S3Options` and every `credentials` an `Option<S3Credentials>`.

```rust
S3Storage::new(config, bucket, prefix, credentials, can_write,
               extra_read_headers, extra_write_headers,
               legacy_rooted_keys: Option<bool>)          -> Result<S3Storage, StorageError>
s3_storage(config, bucket, prefix, credentials,
           legacy_rooted_keys: Option<bool>)              -> StorageResult<S3Storage>
new_s3_storage(config, bucket, prefix, credentials,
               legacy_rooted_keys: Option<bool>)          -> StorageResult<Arc<dyn Storage + Send + Sync>>
new_r2_storage(config, bucket, prefix, account_id, credentials,
               legacy_rooted_keys: Option<bool>)          -> StorageResult<Arc<dyn Storage + Send + Sync>>
new_tigris_storage(config, bucket, prefix, credentials, use_weak_consistency,
                   legacy_rooted_keys: Option<bool>)      -> StorageResult<Arc<dyn Storage + Send + Sync>>

new_http_storage(base_url, config, headers: Option<HashMap<String, String>>)
ObjectStorage::new_http(url, config, headers: Option<HashMap<String, String>>)
ObjectStoreFetcher::new_http(url, opts, headers: &HashMap<String, String>, settings: storage::Settings)

// sealed trait icechunk::virtual_chunks::ChunkFetcher
fetch_part(&self, chunk_location: &Url, key: &str, range, checksum) -> …
fetch_chunk(&self, chunk_location: &Url, key: &str, range, checksum) -> …
```

`legacy_rooted_keys`: `None` probes storage once, `Some(true)` forces the legacy
leading-slash layout (only valid with an empty prefix), `Some(false)` forces the
standard layout. Construction is lazy: none of these constructors performs I/O,
so the verifier builds each of them with no endpoint reachable.

### Shape changes

| item | change | why it breaks |
| --- | --- | --- |
| `SnapshotInfo` | `+ pub pruned_ancestor_tx_logs: Vec<SnapshotId>` | struct literals and exhaustive destructuring |
| `S3Options` | `+ #[non_exhaustive]`, `+ pub checksum_algorithm: Option<S3ChecksumAlgorithm>`, `+ explicit impl Default` | external struct literals are impossible; build with `S3Options::default().with_*(…)` |
| `ObjectStoreConfig` | `Http(HashMap<String, String>)` → `Http(HttpConfig)` | payload type |
| `Credentials` | `+ LocalFileSystemAccess`, `+ HttpAccess` | exhaustive matches |
| `AzureCredentials` | `+ Anonymous` | exhaustive matches |
| `HttpObjectStoreBackend` | `+ pub headers: Option<HashMap<String, String>>` | struct literals |
| `Storage` (sealed) | `+ provided can_create_repository`, `+ provided create_location_if_needed` | non-breaking |
| `ObjectStoreBackend` | `+ provided restricts_empty_prefix_creation`, `+ provided create_location_if_needed` | non-breaking |

`S3Options`' fields are public and readable back off a built value:
`region`, `endpoint_url`, `allow_http`, `force_path_style`, `anonymous`,
`requester_pays`, `network_stream_timeout_seconds` and `checksum_algorithm`.

`S3Options` builders, all consuming and returning `Self`: `with_region`,
`with_endpoint_url`, `with_anonymous`, `with_allow_http`,
`with_force_path_style`, `with_network_stream_timeout_seconds`,
`with_requester_pays`, `with_checksum_algorithm`. New enum
`icechunk::config::S3ChecksumAlgorithm { Crc32, Crc32c, Crc64Nvme, Sha1, Sha256 }`
(`#[serde(rename_all = "snake_case")]`), default `None`.

### Additive error variants (their enums are already `#[non_exhaustive]`)

`IcechunkFormatErrorKind::{InvalidIcechunkHeaderSize, UnknownFileType}`,
`VirtualReferenceErrorKind::UnsupportedObjectKeyForBackend`,
`RepositoryErrorKind::EmptyPrefixCreation`,
`SessionErrorKind::{MissingPrunedAncestorTxLog, MoveIntoSelfOrDescendant,
MoveDestinationParentMissing, MoveDestinationParentNotGroup}`.

The four `SessionErrorKind` variants are struct variants with these fields:

```rust
MissingPrunedAncestorTxLog  { snapshot: SnapshotId, tx_log: SnapshotId }
MoveIntoSelfOrDescendant    { from: Path, to: Path }
MoveDestinationParentMissing{ to: Path, missing_parent: Path }
MoveDestinationParentNotGroup { to: Path, parent: Path }
```

Only `MissingPrunedAncestorTxLog` has in-scope *behaviour* at this milestone
(§2.3, E6); the three `Move*` variants must exist with these shapes, and their
semantics are out of scope (§11).

### Behaviour changes at unchanged signatures

1. `VirtualChunkLocation::from_url` no longer normalizes non-`file://` URLs (§8).
2. `Session::move_node` into a missing destination parent now raises
   `MoveDestinationParentMissing { to, missing_parent }` where the previous
   milestone raised `AncestorNodeNotFound { prefix }`.
3. `Repository::create` on a cloud store at an empty prefix fails with
   `EmptyPrefixCreation` before any I/O (§7).
4. `Repository::open` on local-filesystem storage no longer creates the
   directory; `Repository::create` does.
5. `initialize_tracing` writes to **stderr** by default;
   `ICECHUNK_LOG_TO_STDOUT` restores stdout.
6. `Storage::root_is_clean` treats `StorageErrorKind::ObjectNotFound` from
   `list_objects` as clean instead of propagating.
7. `diff`, `rebase`, `inspect::transaction_log_json` and `garbage_collect` all
   changed around `pruned_ancestor_tx_logs` (§2.3).

### Build and toolchain

`rust-version` **lowered** to `1.91.1`; new workspace lint allowance
`unexpected_cfgs = { level = "warn", check-cfg = ['cfg(feature, values("shuttle"))'] }`;
new `otel` Cargo feature (non-default); `shuttle` became a real feature.
`icechunk::shutdown_telemetry()` exists in both feature states — a no-op without
`otel`.

### Changed since milestones 02 and 06 — the read-only session boundary moved

One inherited rule now says the **opposite** of what an earlier milestone's
corpus said, and it is in scope here. It appears in none of the tables above
because the signatures did not move, so it is called out on its own.

| since | rule | then | now |
| --- | --- | --- | --- |
| milestone 02 | a **per-node recorder** — `add_group`, `add_array`, `update_array`, `update_group`, the `delete_*` family, `set_chunk_ref`, `move_node` — called on a read-only session | milestone 02's `PROFILE.md` §D6 checked it **in neither direction**: those calls only mutate an in-memory change set, so refusing and accepting were equally acceptable | it must **refuse**, with `SessionErrorKind::ReadOnlySession`, as §9 states |
| milestone 06 | the same recorders | milestone 06's `PROFILE.md` P2-2, and the matching line in its `TEST_INTERFACE.md`, went further and required them to **succeed** — "a read-only session accepts edits into its change set but cannot commit", the refusal being `commit`'s alone | that rule is **withdrawn**. Upstream moved the check into the change-set accessor at the milestone-09 release, so the recorder itself now fails and never reaches `commit` |

So the boundary that milestones 02-08 drew at *publishing* — `commit`, `merge`,
`rebase`, and the key-level store writes `set`, `set_if_not_exists`,
`set_virtual_ref`, `delete` — now also covers every recorder. Nothing that
refused before is allowed to succeed now; the set of refusing calls only grew.

A session is read-only when it came from `readonly_session`, and also once it
has committed — a committed session self-demotes, as it has since milestone 02,
and a recorder called on it afterwards reports the same error.

---

## 11. What is **out of scope**

Stated so a correct implementation is not penalised for a choice the verifier
never looks at, and so the excluded requirements are excluded on the record
rather than by silence.

* **The typed, closable async iterator is Python-only.** `AsyncCloseableIterator`
  (formerly `PyAsyncGenerator`), its `__class_getitem__` and its `aclose()` live
  in `icechunk-python/src/streams.rs`. The Rust APIs still return a bare
  `impl Stream<Item = …> + Send` / `BoxStream`, so there is **no Rust-observable
  change** here and nothing is checked. The same applies to the
  `DeprecationWarning` for `None` credentials, the
  `icechunk.credentials.{LocalFileSystemAccess, HttpAccess}` module singletons,
  `icechunk.shutdown_telemetry()`'s `atexit` registration, the `abi3-py312`
  wheels, the `icechunk.zarr` → `icechunk.store` rename, and the wheel-platform
  changes.
* **The OTLP exporter.** Span export sits behind the non-default `otel` Cargo
  feature and needs a live OTLP/gRPC collector, which the verifier does not run.
  `ICECHUNK_OTLP_ENDPOINT`, `OTEL_EXPORTER_OTLP_ENDPOINT`,
  `ICECHUNK_OTEL_SERVICE_NAME`, `ICECHUNK_OTEL_FILTER`, `ICECHUNK_LOG`,
  `ICECHUNK_LOG_TO_STDOUT` and `ICECHUNK_NO_LOGS` are documented in
  `guide/guides-observability.md`; **none of them is in scope**, and no log or
  span output is asserted anywhere.
* **Reading a pre-existing rooted repository.** The `KeyLayout` probe
  (`KeyLayout::{Standard, LegacyRoot}`, `DEFAULT_LAYOUT_ANCHORS`) issues HEAD
  requests against a live S3 endpoint; upstream covers it only with
  rustfs/MinIO containers. Which keys a chosen layout then produces is out of
  scope. The **rejection** half of the empty-prefix change *is* in scope (§7),
  because that gate runs before any I/O, and so is what `legacy_rooted_keys`
  does at **construction** time: all three values are passed to the constructors
  (§10) and `Some(true)` is required to build at the bucket root and to be
  refused under a non-empty prefix, neither of which touches the network.
* **Fetching virtual chunk bytes over HTTP.** Port honouring is checked through
  the location, container-matching and resolution surface (§8), not by making a
  request. No test binds or connects to a socket.
* **Anything needing a cloud endpoint**: S3, GCS, Azure, Tigris, R2, MinIO,
  rustfs. Cloud-shaped handles are only ever constructed and interrogated.
* **Node-move semantics.** `Session::move_node`'s rejection categories changed
  at this milestone (`MoveIntoSelfOrDescendant`,
  `MoveDestinationParentMissing`, `MoveDestinationParentNotGroup`, replacing
  `AncestorNodeNotFound` on the missing-parent path — §10). They are listed
  there because a candidate must implement them, but **no requirement group drives
  `move_node` at this step**; the constraints in
  `guide/guides-moving-nodes.md` — `## Constraints` are out of scope here.
* **Message strings and `Debug`/`Display` renderings.** Errors are matched by
  variant category through `ICError { kind, .. }`, never by rendered text. The
  redaction of header values from `HttpObjectStoreBackend`'s `Display` is a
  documented courtesy, not an in-scope assertion. CLI stdout is in scope for exit
  status and line count, not for formatting.
* **Internal layout.** Module paths below the `icechunk::` surface named here,
  private helpers (`reparent_and_prune`, `rebase_one_log`,
  `warn_deprecated_none_credential`, `scheme_host_port`, `fetcher_cache_key`,
  `SyntheticCompositeInspect`), field order in serialized JSON, allocation,
  concurrency and timing.
* **The candidate's own test suite.** It is never run and never determines
  acceptance. `/app/icechunk/tests/*` may be edited freely.
* **Build identity and schema text.** §1's workspace member list, the `2.1.0`
  version on every internal crate, `Cargo.lock` being committed and pinning
  `icechunk 2.1.0`, `rust-version = 1.91.1`, `[lib] bench = false`, and §2.1's
  claim that `manifest.fbs`/`snapshot.fbs` are byte-identical while
  `common.fbs`/`transaction_log.fbs` changed comments only, all remain
  obligations of the handoff. **No requirement group asserts any of them**: the
  driver reads no manifest, no lockfile and no `.fbs` file. They are attested
  only by `cargo build`, the `--features cli` build and the driver's
  `--locked --offline` resolution all succeeding, which is the whole of the
  consequence for a candidate.
* **Constructors typed in crates this crate does not re-export.**
  `ObjectStorage::new_http(url: &Url, …)` and
  `ObjectStoreFetcher::new_http(url: &Url, opts, headers, settings)` take
  `url::Url` and `object_store::ClientConfigKey`, and neither type is reachable
  at an `icechunk::` path, so a driver restricted to `icechunk::` (§1) cannot
  call them. Both must exist with the signatures §4 and §10 give; the header
  behaviour behind them is checked through `new_http_storage` and
  `HttpObjectStoreBackend::mk_object_store` instead.
* **That transport options survive header installation.** §4 requires headers
  and options not to clobber each other, and the verifier constructs a backend
  with both and requires it to succeed — but the merged `ClientOptions` are
  `object_store`'s, `HttpObjectStoreBackend::config` is typed with
  `object_store::ClientConfigKey`, and neither is reachable at an `icechunk::`
  path (see the entry on constructors typed in crates this crate does not
  re-export). There is therefore no `icechunk::`-level observation that reads an
  option back out, and none is asserted.
* **`repo.fbs`'s "an empty vector is never written".** The writer-side half of
  2.0-reader compatibility (§2.1) is a real obligation of the format, but a
  reader — this release's or a 2.0 one's — cannot distinguish an absent vector
  from an empty one through any `icechunk::` API, and the verifier reads no
  `.fbs` file and no raw flatbuffer table. The *reader* side is in scope (§2.2, an
  object without the field reads as "never expired"); the writer side is not.
* **Node-path sort order.** `common.fbs` and `transaction_log.fbs` changed their
  ordering rules at this release (§2.1, `PROFILE.md` R13). No requirement group
  asserts the order of nodes inside a snapshot or a transaction log, and
  `moved_nodes` is only written by `move_node`, which is out of scope above.
* **The removal of `S3Storage::get_path_str`.** A removal is observable only as
  a compile failure of code nobody writes. §10 records it; nothing checks it.
* **`ChunkFetcher::{fetch_part, fetch_chunk}`'s new `key` parameter.** The trait
  is sealed, so it cannot be implemented outside the crate, and calling either
  method needs a live endpoint — already out of scope above. §10 records the
  signature.
* **An *existing* empty-prefix repository still opening.** That needs a
  repository already present at a bucket root, i.e. a live cloud endpoint. Only
  the refusal half of §7 — which runs before any I/O — is in scope, together with
  the local-filesystem half of the same change (`open` does not create the
  location, `create` does).
* **The `otel` and `shuttle` Cargo features and the `unexpected_cfgs` lint
  allowance.** The verifier builds the default feature set and `--features cli`
  and nothing else, so neither feature is ever compiled and no lint output is
  read. `icechunk::shutdown_telemetry()` is called only in the default,
  no-`otel` state, where §10 requires it to be a no-op.
* **"Do not edit anything under `/app/icechunk-spec/`".** That is an instruction
  to the session, not a behaviour of the artifact; no group inspects the
  corpus.
