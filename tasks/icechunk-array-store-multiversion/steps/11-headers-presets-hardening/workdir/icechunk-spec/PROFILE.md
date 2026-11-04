# Benchmark compatibility profile

Task-owned resolutions where the Icechunk specification beside this file leaves
behaviour open, **or where the specification and the milestone this benchmark
scores disagree**. The specification documents in this directory are normative
and unmodified; this file is the only place the benchmark adds to them, and
where the two conflict **this file wins**, because this file is what the hidden
verifier was written against.

This is the **eleventh and last** milestone of the ladder (upstream `v2.2.0`),
so this file carries every earlier resolution forward. Each is restated **as it
holds at this release**, not as it was written for the release that introduced
it — several were superseded by a later format or API change, and where that
happened the entry says so and gives the current rule. Nothing here is a
historical note: every sentence describes the artifact this step is scored on.

Section A is the corpus and the specification generations. Section B is the
format and the API. Section C is what this release itself resolves. Section D
is the standing out-of-scope list.

---

# A. The corpus and the specification generations

## P1 — `spec.md` is spec version 2.1, and it is the normative one

`spec.md` in this directory is the Icechunk specification at **spec version
2.1**, published with `2.1.0` and unchanged at `2.2.0`. `reference/spec-v2.md`
and `reference/spec-v1.md` are the two earlier published generations, kept for
reading; `archive/spec-alpha1.md` is the original pre-1.0 document.

**Resolved:** the format this milestone writes is **spec 2.1**, and the
top-level `spec.md` defines it. Where `archive/spec-alpha1.md`,
`reference/spec-v1.md` or `reference/spec-v2.md` say something different, they
describe a generation this release no longer writes and they are not a
requirement.

Two further copies of the specification are in the tree and **neither is
normative**, because the documentation overlay merges rather than replaces (see
`README.md` beside this file, "Superseded files"):

- `reference/spec.md` — the **spec-2.0** generation, under the unlabelled name
  it carried at milestone 09. It is the same document upstream later
  republished as `reference/spec-v2.md`.
- `guide/spec.md` — an older copy still, from the milestones that published the
  specification out of the guide directory.

Where either disagrees with the top-level `spec.md`, the top-level one wins, and
where this file disagrees with any of them, this file wins.

## P2 — Spec 2.1 does not change the header byte

Spec 2.0 and spec 2.1 are the same on-disk generation as far as the file header
is concerned: both write spec-version byte **`2`**. There is no
`SpecVersionBin::V2_1`. Spec 2.1 is additive — a 2.0 reader skips what it does
not know, a 2.1 reader treats absence as "not present".

**Resolved and scored:** `SpecVersionBin { V1 = 1, V2 = 2 }`, `V2` is
`Default` and `current()`, `TryFrom<u8>` accepts `1` and `2` and rejects
everything else, and `Display` renders `"1.0"` and `"2.0"`.

## P3 — The prose documents are reading material, not a second contract

`guide/`, `design/` and `reference/` are copied verbatim from upstream. They
explain the system and record its design history. Many of them describe the
**Python** API — `icechunk.Repository`, `icechunk.s3_storage`,
`icechunk.hf_storage`, `to_icechunk` — which this benchmark does not build.

**Resolved:** where a guide shows Python, read it for the *behaviour* it
describes, not for the names. The Rust names this milestone must expose are in
`/app/TEST_INTERFACE.md`, and only those are scored. `reference/changelog.md`
is the normative statement of *what changed* at each release, including this
one.

**And `guide/` carries two earlier generations of itself.** The documentation
overlay merges, so 33 of the 62 files in `guide/` are copies this ladder's
earlier milestones shipped under names upstream has since changed — the
`icechunk-python-*.md` set and a flat unprefixed set (`storage.md`,
`configuration.md`, `quickstart.md`, `version-control.md`, …). They are years
out of date, **nothing in them is normative here**, and `README.md` lists every
one with its successor. Read the successor. In particular
`guide/guides-storage.md` — not `guide/storage.md` — is the copy that carries
this release's `#### Hugging Face` section.

---

# B. The format and the API, as they stand at this release

## P4 — Object ids are 12 bytes; node ids are 8

`archive/spec-alpha1.md` "Reference Files" gives an `ObjectId` JSON Schema with
`"minItems": 16, "maxItems": 16`.

**Resolved:** snapshot, manifest, chunk and attributes ids are **12 bytes**
(`ObjectId<12, _>`); a node id is **8 bytes** (`ObjectId<8, _>`). A 12-byte id
renders and parses as **20 Crockford base-32 characters**, and that string form
is what `Display` and `TryFrom<&str>` use. A byte slice of any other width must
fail to convert. The *serde* form is no longer that string — since the
FlatBuffers format an id is stored as its bytes — and the serde form is not
scored.

## P5 — The physical key layout is never inspected

`archive/spec-alpha1.md` "File Layout" places files under `$ROOT/r/`,
`$ROOT/s/`, `$ROOT/a/`, `$ROOT/m/` and `$ROOT/c/`. The shipped backends have
used different layouts at nearly every release, and at this one a spec-2
repository has a single `repo` table-of-contents object, `snapshots/`,
`manifests/`, `chunks/`, `transaction_logs/` and `overwritten/`, and **no**
`refs/` and no `config.yaml`.

**Resolved:** the benchmark **never inspects object-store keys**, with exactly
one exception, which is not about layout: the CLI group asserts that
`icechunk repo create` leaves behind a repository that `Repository::open` can
open, which it observes as `repo` being a file and `snapshots/` being a
non-empty directory under the configured path. Apart from that, how you name
objects inside a backend is yours, provided every behaviour reachable through
the public API in `TEST_INTERFACE.md` holds. Nothing is scored below the
`Storage` trait.

## P6 — References are entries in the table of contents, not files

`archive/spec-alpha1.md` "Creating and Updating Branches" and "Tags" fix
reference file names and a Crockford-encoded branch sequence number.
`reference/spec-v1.md` fixes `refs/branch.$N/ref.json`.

**Resolved, and superseded twice.** At spec 2 a branch or a tag is an entry
inside the `repo` object, updated under optimistic concurrency. There is **no
observable branch version ladder** at this milestone: `refs::BranchVersion` and
the sequence encoding are gone from the scored surface. What **is** scored is
the behaviour:

- a fresh repository has exactly one branch, `main`, and no tags;
- `create_branch` / `create_tag` fail at a snapshot that does not exist;
- creating a tag whose name already exists fails, and a deleted tag's name is
  retired permanently;
- `delete_branch("main")` fails (`RepositoryErrorKind::CannotDeleteMain`);
- `list_branches` / `list_tags` return `BTreeSet<String>`, and `lookup_branch`
  / `lookup_tag` resolve to a `SnapshotId`.

## P7 — The metadata encoding is FlatBuffers, and it is normative

`archive/spec-alpha1.md` presents JSON Schemas for snapshots and manifests, and
the first shipped implementation used MessagePack. Neither is what this release
writes.

**Resolved:** since spec 1.0 the snapshot, manifest, transaction-log and
repo-info files are **FlatBuffers** buffers, framed by the 39-byte header of P8
and Zstd-compressed. The six schemas `spec.md` quotes inline — `all.fbs`,
`common.fbs`, `snapshot.fbs`, `manifest.fbs`, `transaction_log.fbs`,
`repo.fbs` — are normative. The seventh file in that directory,
`object_ids.fbs`, is **not**: it is a superseded fragment left by milestones 04
to 08, and `ObjectId12` / `ObjectId8` are defined in `common.fbs` at this
release.

What the benchmark scores is narrower than the schemas: a buffer this library
writes must round-trip through `Snapshot::bytes()` →
`Snapshot::from_buffer(spec_version, buffer)`, `from_buffer` must **verify** the
buffer up front so that a truncated or corrupt one is an error rather than a
later panic, and what a snapshot *contains* — its nodes, their `user_data`, the
chunk references reachable from them, its message, its metadata and its
ancestry — must be observable through the API. No driver decodes a buffer by
hand, and no driver compares two implementations' bytes.

## P8 — Every metadata file begins with the same 39-byte header

`spec.md` "Binary File Format" fixes it: a 12-byte `ICE🧊CHUNK` magic, a
24-byte space-padded implementation name, one spec-version byte, one file-type
byte, one compression byte.

**Resolved, and scored:**

- `ICECHUNK_FILE_HEADER_LEN == 39`, `ICECHUNK_SPEC_VERSION_OFFSET == 36`,
  `ICECHUNK_FILE_TYPE_OFFSET == 37`, `ICECHUNK_COMPRESSION_OFFSET == 38`,
  `ICECHUNK_IMPL_NAME_LEN == 24`, and the magic is 12 bytes long;
- `parse_file_header` **discovers** the file type rather than asserting one, and
  rejects both a short buffer and a wrong magic;
- the implementation name this library writes is
  `ICECHUNK_CLIENT_NAME == "ic-" + ICECHUNK_LIB_VERSION`, from which `app_name`
  is the part before the first `-` and `app_version` the part after it;
- every metadata file this library writes is **Zstd**-compressed, so its
  compression byte is `Zstd`. Chunk objects carry no header at all.

## P9 — User attributes are opaque bytes; there are no attributes files

`archive/spec-alpha1.md` "Attributes Files" describes a separate file for
user-defined attributes and flags it not implemented. It is still not
implemented, and the inline mechanism that briefly replaced it is gone too.

**Resolved:** a node's Zarr metadata is a single opaque
`NodeSnapshot::user_data: Bytes` that round-trips **byte for byte**, unknown
Zarr members included. `UserAttributesSnapshot` and `Snapshot::attribute_files`
do not exist. `FileTypeBin::Attributes` remains a defined header byte and
`GCSummary::attributes_deleted` remains a field, but nothing writes an
attributes file and nothing is scored on either.

## P10 — Inline chunks: the default threshold is 512 and the predicate is `>`

`spec.md` does not state a size threshold; inlining is an implementation policy.

**Resolved, and scored:** `RepositoryConfig::default().inline_chunk_threshold_bytes()
== 512`, the threshold is a `u16`, and it is settable per repository through
`RepositoryConfig::inline_chunk_threshold_bytes: Option<u16>`. A payload of
exactly the threshold is still inlined; only a larger one is materialised.
Setting it to `Some(0)` forces every written chunk to be materialised, which is
how the storage-accounting group separates native from inline bytes.

`unsafe_overwrite_refs` does not exist at this release.

## P11 — Error granularity: the category, never the message

Every public error type is an `ICError<…Kind>` pair: `SessionError` /
`SessionErrorKind`, `RepositoryError` / `RepositoryErrorKind`, `StoreError` /
`StoreErrorKind`, `StorageError` / `StorageErrorKind`, `IcechunkFormatError` /
`IcechunkFormatErrorKind`. A match therefore goes through `err.kind`.

**Resolved:** the benchmark asserts an error's **category** — the enum variant,
and the payload only where `TEST_INTERFACE.md` names that payload — and nothing
finer. It never asserts a message, a `Display` or `Debug` rendering, or a source
chain. Every `…Kind` is `#[non_exhaustive]`, so adding variants is safe.

## P12 — Ordering guarantees

Where `spec.md` does not fix an order, none is required and none is asserted:
every driver that observes a collection whose order is not fixed sorts it before
comparing.

**Three orders are fixed and are scored:**

- `Repository::ancestry` yields snapshot info **newest first**, ending at the
  initial commit;
- `Repository::ops_log` yields updates **newest first**;
- `SnapshotInfo::pruned_ancestor_tx_logs` is **oldest first**.

## P13 — Storage backends in scope, and the sealed traits

**In scope, and all offline:** `new_in_memory_storage()` and
`new_local_filesystem_storage(&Path)` over a `tempfile::TempDir`, plus
`icechunk::storage::latency::LatencyStorage::new(inner, write_delay_ms,
read_delay_ms)`, which is a pure wrapper that adds no I/O of its own. Both
backends support conditional create-if-not-exists, so the whole of `spec.md`
"Storage Operations" is observable offline.

`LatencyStorage` applies each delay **before** it delegates, never after: a
write sleeps `write_delay_ms` and *then* calls the inner store's write, so the
object the inner store finally stamps carries a `created_at` at least that much
later than any timestamp the caller took before the call. That ordering is what
makes the `(flushed_at, created_at]` window of P22 openable without sleeping on
a wall clock, and a scored group depends on it.

**Constructed but never connected:** the cloud presets. `new_s3_storage`,
`s3_storage`, `new_r2_storage`, `r2_storage`, `new_tigris_storage`,
`tigris_storage`, `new_hf_storage`, `new_s3_object_store_storage`,
`new_gcs_storage`, `new_azure_blob_storage`, `new_http_storage`,
`new_redirect_storage` must all exist with the signatures `TEST_INTERFACE.md`
gives, and the ones the presets group exercises must be **constructible without
a credential, a socket or a DNS lookup**, because the client behind them is
built lazily. Their configuration is then read back through the synchronous
`Storage::storage_info()`. Nothing is ever connected.

`Storage`, `AssetManager` and the virtual-chunk resolver are **sealed**: they
may only be implemented inside the workspace. No driver implements any of them
and no mock backend exists.

## P14 — Virtual chunk references: containers, prefixes and credentials

`spec.md` "Chunk Files" allows a chunk reference to point into a foreign file.
How that file is reached is configuration, and it has been tightened three
times.

**Resolved, as it stands now:**

- there are **no default containers**. A virtual reference is only resolvable
  through a `VirtualChunkContainer` the repository was opened with, and access
  to it must be authorized explicitly through
  `authorize_virtual_chunk_access: HashMap<String, Option<Credentials>>` keyed
  by the container's **url prefix**;
- a container's `url_prefix` **must end in `/`**; `VirtualChunkContainer::new`
  rejects one that does not. That is what stops `s3://foo/` from authorizing
  `s3://foo-production/`;
- the no-credential cases have explicit sentinels,
  `Credentials::LocalFileSystemAccess` and `Credentials::HttpAccess`. Passing
  `None` still works but is deprecated;
- the only scheme whose **resolution** is scored is `file://`, read from a
  `tempfile::TempDir` with no client of any kind. `s3://`, `gcs://` and
  `vcc://` locations must be **constructible** and must round-trip through
  `VirtualChunkLocation::from_url(..).url()`, but none is ever fetched;
- a reference whose location matches no authorized prefix must **fail to read**
  rather than be read.

## P15 — The Zarr metadata subset that must parse

The `Store` accepts Zarr v3 metadata documents at `zarr.json` keys. Only this
subset is scored:

- group documents: `zarr_format`, `node_type` (the word `group`), optional
  free-form `attributes`;
- array documents: `zarr_format`, `node_type` (the word `array`), optional
  `attributes`, `shape`, `data_type`, `chunk_grid` (name `regular`, with a
  `chunk_shape`), `chunk_key_encoding` (name `default`, separator `/`),
  `fill_value`, `codecs`, optional `storage_transformers`, optional
  `dimension_names`.

**Out of scope:** the Zarr array format itself. Codecs are recorded as opaque
name/configuration pairs and never executed, no chunk is ever decoded, no fill
value is ever materialised, and rectilinear chunk grids — which the format
supports — are not exercised by any scored group.

## P16 — The Zarr key grammar the store answers to

`zarr.json` and `<path>/zarr.json` are metadata keys; `<array>/c/<i>/<j>/…` is a
chunk key, with `/` as the separator.

**Resolved, and scored:** `Store::{get, set, exists, delete, delete_dir, list,
list_prefix, list_dir}` over those keys, `ByteRange::ALL` and
`ByteRange::bounded(start, end)` for ranged reads, and:

- `list` yields every key, `list_prefix` every key under a prefix, `list_dir`
  the immediate children (a group's children as bare names, an array's as
  `zarr.json` and `c`);
- `list_prefix` **rejects** a prefix that is neither the root nor an existing
  group or array, with `StoreErrorKind::BadKeyPrefix { prefix, message }`.
  `list_dir` does **not**: given a prefix inside an array's chunk space it walks
  up to the closest ancestor array and lists chunk prefixes, and given a prefix
  with no ancestor node at all it yields nothing.

## P17 — Sessions, commits and conflicts

**Resolved, and scored:**

- a session is obtained from the repository (`writable_session`,
  `readonly_session`, `rearrange_session`) and a session that has committed
  becomes **read-only**: `read_only()` is true, `branch()` is `None`, and any
  further edit fails;
- `Session::commit(message)` returns a **`CommitBuilder`**; the commit happens
  at `.execute().await`. `commit_rebasing` and `flush` do not exist;
  `.anonymous()` replaces `flush`;
- two writable sessions on one branch may both mutate, and the second to commit
  fails with `SessionErrorKind::Conflict { expected_parent, actual_parent }`;
- `Session::rebase(solver)` — or `commit(m).rebase(solver, attempts)` — resolves
  it, with `BasicConflictSolver`'s `on_chunk_conflict` deciding whose chunk
  wins;
- `move_node` requires a **rearrange** session (`NonRearrangeSession`
  otherwise), and an ordinary edit on a rearrange session fails
  (`RearrangeSessionOnly`).

## P18 — Configuration merge

`RepositoryConfig` is all-`Option` with an accessor per field that supplies the
default, and `merge(&self, other)` is field by field with `other` winning.

**Resolved, and scored:** the one field that does **not** merge field-wise is
`virtual_chunk_containers`, which `merge` **replaces** outright. Two defaults
are scored: `inline_chunk_threshold_bytes() == 512` and
`max_concurrent_requests() == 256`.

## P19 — Expiration and garbage collection

`design/007-basic-expiration.md` and `design/011-IC2-gc-and-expiration-consistency.md`
describe the design; `guide/understanding-expiration.md` the behaviour.

**Resolved, and scored:**

- `expire` releases every snapshot older than the cutoff that no reference
  protects, and **edits in place** the oldest surviving snapshot of each
  ancestry so that it re-parents onto the repository root. It returns
  `ExpireResult { released_snapshots, edited_snapshots, deleted_refs }`;
- `garbage_collect` then deletes the released files and reports a `GCSummary`;
- a `dry_run` configuration reports what it would delete and deletes nothing;
- expiration records `SnapshotInfo::pruned_ancestor_tx_logs`, oldest first, and
  garbage collection keeps every transaction log a retained snapshot still
  names — see P22;
- how many snapshots a history squashes into, and how many objects a collection
  touches beyond the counters `GCSummary` names, are **not** scored.

## P20 — The table of contents

`design/010-notes-towards-an-IC-2.md` and `design/012-some-more-IC2-format-changes.md`
describe it; `spec.md` "Repo Info File" defines it.

**Resolved, and scored:** at spec 2 the branches, tags, configuration,
repository metadata, status, feature flags and an operations log all live in one
conditionally-written object. `Repository::ops_log()` exposes that log, newest
first, and it records at least the repository's creation
(`UpdateType::RepoInitializedUpdate`), each commit
(`NewCommitUpdate`) and each reference change (`TagCreatedUpdate`,
`BranchCreatedUpdate`, …). Setting the status to `RepoAvailability::ReadOnly`
makes `writable_session` fail. How many backup copies the implementation keeps
under `overwritten/`, and how it retries a contended update, are not scored.

**A repository always declares at least one feature flag**, so
`Repository::feature_flags()` never yields an empty iterator: the flag set is a
property of the format generation rather than of anything the repository has
been asked to do, and a freshly created one already has it. *Which* flags there
are, and what each is called, are **not** scored — only that at least one is
reported and that `FeatureFlag::name()` answers for it.

---

# C. What this release resolves

## P21 — "The `inspect_*` methods report a header" means the JSON, not a type

`reference/changelog.md` → `## Python Icechunk Library 2.2.0` → `### Features`,
bullet 1, says the `inspect_*` methods now report a `header` for the file they
read: the library version that wrote it, and that file's spec version, file type
and compression.

**Resolved:** the four Rust entry points `snapshot_json`, `repo_info_json`,
`manifest_json` and `transaction_log_json` keep their signatures, and **all
four** now emit a top-level `header` member. Every `*Inspect` struct behind that
JSON is `pub(crate)` or private, so **the contract is the JSON text**, not a
type: an external test parses the returned `String`. `TEST_INTERFACE.md` §3.1
fixes the six member names, their value domains, and the rule that
`app_version` is **omitted** rather than null when the implementation string has
no `-`.

The header a document reports must agree with what
`AssetManager::fetch_*_header` reads out of that same file.

For `transaction_log_json` the header describes **the snapshot's own
transaction-log file only**; when a collapsed ancestry produces a
`synthetic_composite`, the merged pruned-ancestor logs' own headers are not
reported.

## P22 — "Clock skew" means the object store's `created_at`, not the host's `flushed_at`

`reference/changelog.md` → `2.2.0` → `### Fixes`, bullet 1: garbage collection
no longer deletes still-referenced transaction logs when the host and
object-store clocks are skewed.

**Resolved:** the retention decision for the repository info now uses the same
clock as the physical delete — the storage object's `created_at`, which
`AssetManager::list_snapshots()` reports — instead of the host-stamped
`SnapshotInfo::flushed_at`. Judging it by `flushed_at` half-deleted a snapshot
in the `(flushed_at, created_at]` window: it was dropped from the repository
info, losing its `pruned_ancestor_tx_logs` references, while its file survived.

**Scored, end to end:** with both `GCConfig::clean_all` cutoffs set exactly to a
retained snapshot's storage `created_at`, that snapshot stays in the repository
info with its pruned references intact, every transaction log they name is
still fetchable, and `GCSummary::transaction_logs_deleted == 0` — while a
genuinely released snapshot's file is still collected. A snapshot the storage
listing does not yet show is retained too.

No public signature changed; the widening of the window in a test is done with
`LatencyStorage`, not by sleeping.

## P23 — "Readable under both spellings" is observable only as constant values

`reference/changelog.md` → `2.2.0` → `### Fixes`, bullet 5: object metadata keys
no longer contain `_`; they are `icspecver`, `icclient`, `icfiletype`,
`iccompalg` and `icechunkwriteid`, and the old names stay available as
`*_DEPRECATED` constants.

**Resolved, and this is load-bearing:** the four format keys are *write-only
observability metadata*. The authoritative header lives in the file bytes and is
parsed by `parse_file_header` (P8). In this release **no parse path anywhere in
the tree reads either spelling** — searching the whole workspace for
`_DEPRECATED` matches only the definitions themselves. The library writes the
new spellings and reads back only the write-id key.

So "readable under both spellings" is testable **only** as: both constants are
public and carry the documented values, the new one survives a header-stripping
proxy (ASCII alphanumeric, starting with a letter), and the deprecated one is
the pre-rename spelling. A test cannot make the library parse either one, and no
scored group tries. The exact values are in `TEST_INTERFACE.md` §3.5.

The fifth key, the conditional-PUT write id, lives in
`icechunk_storage::readback`, which `icechunk::storage` does **not** re-export.
It is out of scope: see P26.

## P24 — The S3 endpoint-with-path fix has no offline observation point

`reference/changelog.md` → `2.2.0` → `### Fixes`, bullet 6: an S3 endpoint URL
that includes a path used to address `/<namespace><bucket>/<key>` and fail with
`NoSuchBucket`, because the AWS SDK joins the endpoint path and the bucket name
without a separator. Icechunk now appends the missing `/`; endpoints without a
path (Tigris, R2, MinIO) keep the same behaviour.

**Resolved: out of scope, and here is why.** The function that appends the slash
is **private**, and its only caller builds a live AWS SDK client. There is no
offline, public observation point, so **no scored group asserts it**. The
behaviour it must implement is recorded here so that it is still a stated
requirement:

| endpoint | result |
| --- | --- |
| `https://s3.hf.co/my-namespace` | `https://s3.hf.co/my-namespace/` |
| `https://s3.hf.co/my-namespace/` | `https://s3.hf.co/my-namespace/` |
| `http://localhost:9000/a/b` | `http://localhost:9000/a/b/` |
| `https://fly.storage.tigris.dev` | unchanged |
| `http://localhost:9000` | unchanged |
| `http://localhost:9000/` | unchanged |
| `localhost:9000` | unchanged (no `://`) |

i.e. a trailing `/` is appended exactly when the URL has a `scheme://` **and**
something after the host, and does not already end in `/`.

The half that **is** scored is the input side: `new_hf_storage` records
`endpoint_url` as `https://s3.hf.co/<namespace>` with **no** trailing slash,
because the slash is added when the client is built, not when the configuration
is made.

## P25 — The Hugging Face preset, and what of it is observable

`reference/changelog.md` → `2.2.0` → `### Features`, bullet 2, and
`guide/guides-storage.md` → `#### Hugging Face`.

**Resolved:** the Rust surface is
`icechunk::storage::new_hf_storage(config, bucket, prefix, namespace,
credentials, extra_read_headers, extra_write_headers, legacy_rooted_keys)`. The
guide's `icechunk.hf_storage(...)` is the Python wrapper of it and is not built
here. The preset sets the endpoint to `https://s3.hf.co/<namespace>`, defaults
the region to `us-east-1` **only when none was given**, forces path-style URLs,
and sends the **bare bucket name**.

`S3Storage`'s `config` field is private, so the observation point is the
synchronous `Storage::storage_info()`; `TEST_INTERFACE.md` §3.7 fixes what it
reports and how the booleans render.

**Not scored, and stated so:** the credential shape the guide describes — an
access key id starting with `HFAK`, generated from a Hugging Face access token
— is a property of the service, not of this library; `S3Credentials::Static`
accepts any strings. And the documented consistency limitation — the gateway
discards user metadata, so a conditional write whose response is lost can report
a conflict that never happened, and the remedy is to retry the commit — needs
a lost response from a real gateway to observe. Neither is reachable offline.

---

# D. Standing out of scope

## P26 — What no scored group reaches

- **The network, cloud credentials and containers.** Every scored group runs
  offline. MinIO, Azurite, a GCS emulator and a live Hugging Face bucket are all
  out of reach, and nothing that needs one is scored.
- **`icechunk_storage::readback`.** `WRITE_ID_METADATA_KEY` and
  `WRITE_ID_METADATA_KEY_DEPRECATED` are not re-exported through
  `icechunk::storage`, and the verifier's driver crate depends only on
  `icechunk`. The conditional-PUT lost-response machinery
  (`design/017-conditional-put-lost-response.md`) is likewise out of scope: it
  needs an injected write id and a real failing store.
- **Zero-copy buffer writes.** `reference/changelog.md` → `2.2.0` →
  `### Performance` says writing to a store no longer copies the NumPy buffer.
  That is a **Python-layer** change: `Store::set(&self, key: &str, value:
  Bytes)` is unchanged, and so is the PyO3 binding beneath it. **No Rust test
  can observe it**, and none does.
- **The asynchronous API.** The `*_async` surface named in
  `guide/guides-async.md` is a Python binding; the Rust crate has been `async`
  throughout since long before. There is nothing Rust-side to score.
- **Python, JavaScript and WebAssembly bindings.** `icechunk-python` and
  `icechunk-js` must stay workspace members and the workspace must build, but
  nothing in either is scored.
- **Interactive CLI commands.** `icechunk config init` and `icechunk config add`
  prompt on a TTY. No scored group runs them.
- **Migration from spec 1.** `icechunk::migrations::migrate_1_to_2` must exist,
  but no scored group runs it: this ladder's artifact has written spec 2 since
  the ninth milestone, and there is no spec-1 repository to migrate.
- **Message wording, internal layout, allocation and timing.** As P5 and P11.
