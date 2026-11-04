# Benchmark compatibility profile

Task-owned resolutions where the Icechunk specification beside this file leaves
behaviour open, **or where the specification and the milestone this benchmark
scores disagree**. The specification document `spec.md` is normative and
unmodified; this file is the only place the benchmark adds to it, and where the
two conflict **this file wins**, because this file is what the hidden verifier
was written against.

`spec.md` was published alongside a repository format that was still moving.
**Seven** of its statements describe a design the shipped format does not
implement; they are P1 to P7 below, each named with the specification section it
comes from and resolved. P8 to P13 resolve what `spec.md` simply leaves open.

---

## P1 — Object ids are 12 bytes, not 16

`spec.md` "Reference Files" gives an `ObjectId` JSON Schema with
`"minItems": 16, "maxItems": 16`.

**Resolved:** every object id in this task — snapshot, manifest, chunk and
attributes ids alike — is **12 bytes**. The public aliases are
`ObjectId<12, _>`; `TEST_INTERFACE.md` section 4.2 gives their names. An id is
rendered and parsed as **Crockford base32** over those 12 bytes, and that string
is also its serde form. A byte slice of any other length must fail to convert.

## P2 — The physical key layout is not scored

`spec.md` "File Layout" places files under `$ROOT/r/`, `$ROOT/s/`, `$ROOT/a/`,
`$ROOT/m/` and `$ROOT/c/`. The shipped backends instead use `snapshots/`,
`manifests/`, `chunks/` and `refs` beneath the store prefix, and write no
attributes prefix at all.

**Resolved:** the benchmark **never inspects object-store keys**. Neither layout
is required: how you name the objects inside a backend is yours, provided every
behaviour reachable through the public API in `TEST_INTERFACE.md` section 4
holds. Nothing is scored below the `Storage` trait.

## P3 — Branch reference naming and sequence encoding are not scored

`spec.md` "Creating and Updating Branches" fixes the branch file name as
`r/$BRANCH_NAME/$ENCODED_SEQUENCE.json`, where the sequence is encoded by
subtracting it from `1099511627775`, Crockford base32 encoding the result and
left-padding to eight characters — so that `main` sequence 0 is
`r/main/ZZZZZZZZ.json`. The shipped implementation writes
`refs/branch:$BRANCH_NAME/$CROCKFORD(u64::MAX - version).json` with no padding.

**Resolved:** neither the key nor the encoding is scored, and no driver reads a
reference file directly. What **is** scored is the observable sequence:

- the first successful update of a branch returns `BranchVersion(0)`;
- each subsequent successful update returns the previous version plus one;
- `refs::fetch_branch_tip` returns the snapshot of the latest version;
- a branch name containing `/` is rejected — `spec.md` "Creating and Updating
  Branches" says "Branch names may not contain the `/` character", and that
  sentence is kept.

Reading an *arbitrary historical* branch version is not part of the public API
at this milestone and is not scored.

## P4 — Tag reference naming is not scored

`spec.md` "Tags" fixes the tag file as `r/$TAG_NAME.json`. The shipped
implementation writes `refs/tag:$TAG_NAME/ref.json`.

**Resolved:** not scored, as P3. What is scored is that a tag is created with a
create-if-not-exists operation, that creating an existing tag fails, that a tag
is never updated or deleted once created, that a tag name containing `/` is
rejected, and that an unknown tag is reported as not found.

## P5 — A reference file carries only a snapshot id

`spec.md` "Reference Files" requires each reference JSON object to have the
members `properties`, `snapshot` and `timestamp` (RFC 3339).

**Resolved:** `refs::RefData` has exactly one public field, `snapshot:
SnapshotId`, and the reference file is its JSON serialisation. `properties` and
`timestamp` are **out of scope** at this milestone: no driver writes them, reads
them, or requires them to be absent.

## P6 — Snapshot and manifest encoding is not scored; reference files are JSON

`spec.md` "Snapshot Files" and "Chunk Manifest Files" present JSON Schemas. The
shipped format serialises snapshots and manifests as **MessagePack**, and
declares so in a `format::format_constants` module whose contents are not
themselves scored.

**Resolved:** the encoding of snapshot and manifest files is **yours**; no
driver decodes one. Two consequences are scored, and only these two:

- reference files are JSON — but only through `refs::RefData`'s serde impl, not
  by reading a file;
- `ChangeSet::export_to_bytes` and `ChangeSet::import_from_bytes` must
  round-trip **each other** (`TEST_INTERFACE.md` 4.4). The byte format is
  yours; self-consistency is not.

What a snapshot *contains* is scored, through the API: the nodes it holds,
their metadata, the chunk references reachable from them, its commit message
and its ancestry.

## P7 — Attributes files are not implemented

`spec.md` "Attributes Files" describes a separate file for user-defined
attributes and flags it not implemented.

**Resolved:** it stays not implemented. `format::attributes::AttributesTable` is
a public, fieldless struct, and `Storage::fetch_attributes` /
`Storage::write_attributes` are **not scored** — a driver never calls either,
and an implementation that panics in them loses nothing. User attributes are
carried **inline** in the snapshot, through
`snapshot::UserAttributesSnapshot::Inline`, and that path is scored.

## P8 — Inline chunks: the default threshold is 512 and the predicate is `>`

`spec.md` never states a size threshold; inlining is an implementation policy.

**Resolved, and scored:**

- `RepositoryConfig::default().inline_chunk_threshold_bytes == 512`, and
  `RepositoryConfig::default().unsafe_overwrite_refs == false`;
- `Repository::get_chunk_writer()` produces `ChunkPayload::Ref` when
  `data.len() > threshold` and `ChunkPayload::Inline` otherwise — so a payload
  of **exactly** the threshold is still inline;
- the threshold is a `u16` and is settable per repository, through
  `RepositoryBuilder::with_inline_threshold_bytes`, through
  `RepositoryBuilder::with_config`, and through
  `zarr::RepositoryConfig::with_inline_chunk_threshold_bytes`.

`unsafe_overwrite_refs` turns the conditional reference write into an
unconditional one, deliberately giving up the guarantee `spec.md`
"Storage Operations" states as "Conditional write if-not-exists". Its default
is `false` and no scored group turns it on; it must exist and be settable.

## P9 — Storage backends in scope

Only two backends are exercised, and both are offline:

- `ObjectStorage::new_in_memory_store(Option<String>)`, and
- `ObjectStorage::new_local_store(&std::path::Path)` over a temporary directory,

plus the caching wrappers `MemCachingStorage::new` and
`Repository::add_in_mem_asset_caching`, which add no I/O of their own. Both
backends support conditional create-if-not-exists, so the whole of
`spec.md` "Storage Operations" is observable offline.

**Out of scope:** S3 and every other cloud object store. `S3Storage`,
`storage::s3::mk_client`, `StorageConfig::S3ObjectStore` and any
`S3Credentials` value must exist and must deserialise, because
`zarr::StorageConfig` and `ConsolidatedStore` name them — but nothing is ever
connected, no credential is ever read, and no driver reaches the network. There
is no GCS or Azure backend at this milestone.

`Storage` and `storage::virtual_ref::VirtualChunkResolver` are **sealed**
traits: they may only be implemented inside your crate. No driver implements
either, and no mock backend exists.

## P10 — Virtual chunk references: `file://` only

`spec.md` "Chunk Files" allows a chunk reference to point into a foreign file.

**Resolved:** the only scheme whose *resolution* is scored is `file://`, read
from the local filesystem with no client of any kind. `s3://` locations must be
**constructible** — `VirtualChunkLocation::from_absolute_path("s3://bucket/k")`
succeeds — but are never fetched. A URL that does not parse, and an `s3` URL
with no bucket name, must be rejected by `from_absolute_path`; a well-formed URL
whose scheme is neither `file` nor `s3` must be rejected **when the chunk is
read**, not when the reference is built.

A byte range requested of a virtual chunk composes with the reference's own
`offset` and `length`: the read never starts before `offset` and never passes
`offset + length`.

## P11 — The Zarr metadata subset that must parse

The store accepts Zarr v3 metadata documents at `zarr.json` keys. Only this
subset is scored, and `TEST_INTERFACE.md` section 5 gives the exact shapes:

- group documents: `zarr_format`, `node_type` (which must be the word `group`),
  and optional free-form `attributes`;
- array documents: `zarr_format`, `node_type` (the word `array`), optional
  `attributes`, `shape`, `data_type`, `chunk_grid` (name `regular` with a
  `chunk_shape`), `chunk_key_encoding` (name `default` with separator `/`),
  `fill_value`, `codecs`, optional `storage_transformers`, and
  `dimension_names`.

**Out of scope:** the Zarr array format itself — codecs are recorded as opaque
name/configuration pairs and never executed, chunk grids other than `regular`
and separators other than `/` are not required, no chunk is ever decoded, no
fill value is ever materialised into an array, and no partial decoding of any
kind is performed.

## P12 — Error reporting granularity

Errors are reported through the typed results `RepositoryResult`, `StoreResult`,
`RefResult` and `StorageResult`. The benchmark asserts an error's **category**
— the enum variants `TEST_INTERFACE.md` section 6 lists, and nothing finer. It
never asserts a message, a `Display` or `Debug` rendering, a source chain, or
the payload of a variant except where section 6 names that payload (the two
snapshot ids of a conflict, the key and coordinates of a missing chunk, the path
of a missing node, the key of an invalid key). Every public error enum except
`refs::RefError` is `#[non_exhaustive]`, so you may add variants.

## P13 — Ordering guarantees

Where `spec.md` does not fix an order, none is required and none is asserted.
Every driver that observes a collection whose order is not fixed — the keys a
store lists, the children of a directory, the references in a repository, the
nodes of a snapshot — **sorts it before comparing**.

Two orders *are* fixed and are scored:

- `Repository::ancestry` and `Store::ancestry` yield snapshot metadata
  **newest first**, beginning with the current snapshot and ending with the
  initial one;
- `Store::get_partial_values` returns one result per requested key, in the
  order the keys were requested.
