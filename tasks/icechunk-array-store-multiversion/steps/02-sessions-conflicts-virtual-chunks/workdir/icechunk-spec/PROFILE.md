# Benchmark compatibility profile — milestone 02

Task-owned resolutions where the Icechunk documents beside this file leave
behaviour open, narrow it deliberately, or have fallen behind the release they
ship with. The documents are normative and unmodified; this file is the only
place the benchmark adds to them, and it never contradicts `spec.md`.

Sections `P*` are standing resolutions carried from milestone 01. Sections `R*`
are about what milestone 01 left behind: a resolution this release has
**retired**, because upstream has since made the document agree with the code,
or a milestone-01 document still sitting in this directory that is not part of
this milestone's corpus. Sections `D*` are the
doc ↔ implementation drifts a reader of this release's corpus must know about;
each one names the drift and says which side this benchmark follows.

---

## Standing resolutions carried forward from milestone 01

### P1 — Storage backends in scope

The only storage backends exercised are **in-memory** and the **local
filesystem**. S3, GCS, Azure and Tigris configuration types must be
constructible and must serialise, but no request is ever made to them, no
credential is ever supplied that reaches a network, and nothing in this task
requires MinIO, a cloud account or a metastore. `spec.md` → "Storage
Operations" lists the capabilities a backend must have; those capabilities are
observed through the two local backends only.

### P2 — The Zarr array format is not in scope

Icechunk stores Zarr arrays; it does not define them. Array metadata documents
are carried through the key/value store as opaque JSON, and chunk bytes are
carried opaque. Codecs, data-type semantics, fill-value encoding and chunk
compression are never exercised. What is exercised is that a metadata document
written at a `zarr.json` key comes back, that a chunk written at a
`<array>/c/<i>/<j>/<k>` key comes back, and that the key grammar itself is
honoured.

### P3 — Object serialisation formats

`spec.md` → "File Formats" declares the on-disk encodings to be in flux:
reference files are JSON, snapshots and manifests are MessagePack. This
benchmark never opens an object-store key directly and never asserts an
encoding. What it asserts is round-tripping through the crate's own public API.
Internal storage prefixes and key names are out of scope at every milestone.

### P4 — Undefined ordering

Where a document fixes no order, none is required and none is asserted: the
order of branches and tags in a listing, the order of nodes from `list_nodes`,
the order of chunks from `all_chunks`, the order of keys from `list`,
`list_prefix`, `list_dir` and `list_dir_items`, and the order of locations from
`all_virtual_chunk_locations`. Every such collection is sorted or compared as a
set before it is compared. Where a document *does* fix an order — ancestry is
newest first, and `get_partial_values` answers in the order its key ranges were
given — that order is required.

### P5 — Timestamps

A snapshot's `written_at` is never compared to a wall clock; only that it
exists and survives a round trip. Where a virtual chunk checksum carries a
timestamp, only its relation to the referenced object's own last-modified time
is observed, never its absolute value.

### P6 — Error reporting

Failures are reported through the crate's `Result` types. The benchmark asserts
the **category** an operation fails with — the error enum variant a document or
`TEST_INTERFACE.md` names — and never a message, a `Display` rendering, a
`Debug` rendering or a source chain. Where `TEST_INTERFACE.md` names no
category, only that the operation fails is asserted.

### P7 — The inline-chunk threshold is policy, not format

No specification document states a size at which a chunk is stored inline
rather than materialised. It is a configuration value
(`inline_chunk_threshold_bytes`, documented default 512 bytes in
`guide/icechunk-python-configuration.md` → "Writing chunks inline"), applied as
*larger than* the threshold materialises and *at or below* it inlines. A reader
must accept all three reference kinds regardless of the threshold in force.

### P8 — Attributes files stay unimplemented

`spec.md` → "Attributes Files" carries an explicit warning that attribute files
have not been implemented, and the shipped storage backends leave the
corresponding operations unimplemented. Out-of-line user attributes are
therefore out of scope. **Inline** user attributes, set and read through the
session, are in scope and are exercised.

### P9 — Unsafe reference overwriting

`unsafe_overwrite_refs` (default `false`) deliberately turns the conditional
create-if-not-exists that `spec.md` → "Creating and Updating Branches" relies on
into an unconditional overwrite, for stores without that primitive. The
benchmark only ever exercises the default, so the guarantee that exactly one of
two concurrent commits to a branch succeeds is required.

---

## What milestone 01 left behind

### R1 — `spec.md` was rewritten to match the implementation

Milestone 01's profile had to resolve several places where the alpha.1
specification described something the alpha.1 code did not do. The `spec.md`
shipped at this tag is a different, much shorter document that has closed most
of them, and **the new text is what this milestone follows**:

| Milestone 01 divergence | Status at this release |
| --- | --- |
| The spec said object ids were 16 bytes; the code used 12 | Closed: `spec.md` → "Reference Files" now says "The snapshot id is 12 byte random binary, so the encoded string has 20 characters." |
| The spec put files under `$ROOT/r/ s/ a/ m/ c/`; the code used long names | Closed: `spec.md` → "File Layout" now says `refs/`, `snapshots/`, `attributes/`, `manifests/`, `chunks/` |
| The spec named branch files `r/$BRANCH/$ENCODED.json`; the code used a different prefix | Closed: `spec.md` now says `refs/branch.$BRANCH_NAME/$ENCODED_SEQUENCE.json`, which is what the code writes |
| The spec named tag files `r/$TAG.json`; the code used a directory | Closed: `spec.md` now says `refs/tag.$TAG_NAME/ref.json` |
| The spec required reference JSON with `properties`, `snapshot` and `timestamp`; the code wrote only `snapshot` | Closed: `spec.md` now says "a single key `\"snapshot\"`" |
| The spec showed JSON Schemas for snapshots and manifests; the code wrote MessagePack | Closed: `spec.md` now shows the Rust structures and says MessagePack |

Nothing in that table is scored as a *file layout*: P3 still applies, and these
rows are listed only so that a reader who compares `spec.md` with
`archive/spec-alpha1.md` knows which document to believe. What the rows do
settle is the observable arithmetic: the branch sequence number is subtracted
from `1099511627775`, so a repository admits at most that many commits.

### R2 — One milestone-01 document is still on disk and is not part of this corpus

`/app` is cumulative and a milestone's documents are **merged** into it rather
than replacing the directory, so a file an earlier milestone shipped under a
name this milestone does not use stays behind. At this milestone there is
exactly one: `guide/icechunk-python-developing.md`, milestone 01's copy of a
page upstream deleted before `icechunk-v0.1.0-alpha.8`.

**Resolution.** It is **not** part of this milestone's corpus. It is not listed
in `SHA256SUMS`, it describes a build and test procedure for the alpha.1 tree,
and nothing at this milestone is scored against anything in it. `guide/`
therefore holds 15 files on disk and 14 that belong to this release; every other
file in `icechunk-spec/` was replaced by this milestone's edition. The same
applies at every later milestone: where a document's name changed upstream, the
older name may survive on disk, and only the files a milestone's own
`SHA256SUMS` lists are that milestone's corpus.

---

## Drifts in this release's own corpus

### D1 — The configuration and virtual-dataset guides describe an API that no longer exists

**The drift.** `guide/icechunk-python-configuration.md` → "Virtual Reference
Storage Config" and `guide/icechunk-python-virtual.md` → "Virtual Reference
Storage Support" both document a pre-container design: a `VirtualRefConfig`
object and `StorageConfig.s3_from_env()`-style constructors. Neither exists at
this release.

**Resolution.** Follow `design/001-virtual-chunk-improvements.md`. Virtual chunk
storage is configured as a set of **virtual chunk containers** on the
repository's `RepositoryConfig`, and credentials are supplied separately as a
map keyed by container name when the repository is created or opened. Those two
guide sections are background only and nothing in them is scored. The same
guides' *other* sections — notably "Writing chunks inline", "Creating a new
repo" and "Opening an existing repo" — are current and are followed.

### D2 — The concurrency and version-control guides are empty, so the conflict taxonomy lives nowhere in prose

**The drift.** The two pages that ought to carry this milestone's headline
capability are stubs at this tag.
`guide/icechunk-python-concurrency.md` is fifteen lines: a top-level "TODO:
describe the general approach to concurrency in Icechunk", then four headings —
"Built-in concurrency", "Distributed concurrency within a single transaction",
"Concurrency across uncoordinated sessions" and its sub-heading "Conflict
detection". The first two carry a one-line note to the author about what should
go there; the last two carry nothing at all.
`guide/icechunk-python-version-control.md` reads, in full, "COMING SOON!" and a
link to a different product's documentation.

**Resolution.** `design/002-rebase-commit-improvements.md` is the normative
source for conflicts and rebase at this milestone, and the following
**enumeration of conflict classes is task-owned**, because no document in this
corpus lists them. A conflict is exactly one of:

| Class | Raised when |
| --- | --- |
| new node conflicts with an existing node | we create a node at a path where the intervening commit created one |
| new node in an invalid group | we create a node underneath a path the intervening commit turned into an array |
| Zarr metadata double update | both updated the same array's metadata |
| Zarr metadata update of a deleted array | we updated the metadata of an array the intervening commit deleted |
| user attributes double update | both set user attributes on the same node |
| user attributes update of a deleted node | we set user attributes on a node the intervening commit deleted |
| chunk double update | both wrote the same chunk of the same array; the report carries the conflicting chunk coordinates |
| chunks updated in a deleted array | we wrote chunks into an array the intervening commit deleted |
| chunks updated in an updated array | we wrote chunks into an array whose metadata the intervening commit updated |
| delete of an updated array | we deleted an array the intervening commit wrote to or updated |
| delete of an updated group | we deleted a group the intervening commit updated |

Four of those eleven can be repaired by a conflict-solving policy: the two
delete rows at the end of the table, plus "chunk double update" and "user
attributes double update". The other seven — both node-creation rows, "Zarr
metadata double update", "Zarr metadata update of a deleted array", "user
attributes update of a deleted node", "chunks updated in a deleted array" and
"chunks updated in an updated array" — are always fatal, whatever the policy.
`TEST_INTERFACE.md` section 4.4 gives the exact Rust spelling and says which
policy knob governs which row.

The empty "Distributed concurrency within a single transaction" heading is
covered instead by `guide/icechunk-python-dask.md` → "Icechunk + Dask": writers
must not target the same store chunks, and it is the caller's responsibility to
ensure that.

### D3 — Configuration override is wholesale, not per field

**The drift.** `design/001-virtual-chunk-improvements.md` → "Global config"
says the persisted `RepositoryConfig` "acts as a default, read during repository
instantiation, and overridable by users at runtime", and adds that "Different
fields of configuration may have different behaviors when overridden" —
language that reads as a per-field merge. The two documents disagree:
`design/003-python-instantiation-api.md` → "Discussion" says instead that "a
repo is instantiated with its persistent configuration by default, and the
config can be overwritten by passing the full configuration object", and calls
that out as something it would like to improve — "To alter a single parameter
of the config, users need to retrieve the full configuration, alter it, and
overwrite the full config object."

**Resolution.** Follow `design/003`. At this milestone the override is
**wholesale**: supplying a configuration when a repository is opened or created
replaces the persisted one entirely for that instance, and a field the caller
did not mention takes its value from the configuration type's own default,
**not** from the persisted object. Per-field merging is a later milestone's
capability and is not required here. The other two halves of "Global config"
are required as written: there is exactly one configuration per repository, and
it is written with a conditional write so a writer holding a stale view of it
cannot clobber a newer one.

### D4 — `set_virtual_ref` has no container-validation flag; the check happens at read time

**The drift.** `design/001-virtual-chunk-improvements.md` → "Setting virtual
references" describes a `validate_containers` argument that makes
`set_virtual_ref` verify up front that some container's prefix is a prefix of
the location, "An exception is raised if that's not the case and the reference
is not stored." No such argument exists at this release.

**Resolution.** Storing a virtual reference always succeeds, whatever its URL.
The authorisation check happens when the chunk is **read**, and an unmatched
location fails there with the "no container for this URL" category. This is the
behaviour the same document asks for two sections later, under "New virtual
chunk datastructures": a reference stores only the location URL and "The
resolution from URL to `VirtualChunkContainer` is done at read time." A
milestone that introduces write-time validation will say so; this one must not
reject a write.

### D5 — The default container set is spelled `az`, not `azure`, and its prefixes carry no scheme separator

**The drift.** `design/001-virtual-chunk-improvements.md` → "Default
`VirtualChunkContainers`" says "The names and prefixes of these containers will
be (`s3`, `gcs`, `azure`, `tigris`, etc)."

**Resolution.** The set every repository is created with is exactly five
containers named and prefixed `s3`, `gcs`, `az`, `tigris` and `file` — `az`,
not `azure`, and a `file` container the document does not mention. Each
container's URL prefix equals its name and carries **no** `://` separator, so a
default container matches by scheme name alone. Containers with a longer prefix
still win, per "Reading virtual chunks": sort by prefix length descending and
take the first whose prefix the location starts with. A repository may replace
or clear the default set.

Two consequences follow, and both are required:

- Because matching is a plain string prefix test, a container whose prefix is
  `file://` and a container whose prefix is `file:///some/dir` both match a
  location under that directory, and the longer one is the one used.
- Only the S3 and local-filesystem container backends can actually fetch a
  chunk at this release. A container configured for GCS, Azure, Tigris or an
  in-memory store is constructible and serialisable, but resolving a chunk
  through one is out of scope (P1) and is never exercised.

### D6 — A read-only session is enforced at the publishing boundary, not at every recorder

**The drift.** `guide/icechunk-python-quickstart.md` → "Write some data and
commit" says a committed session "becomes read only to ensure that all writing
is done explicitly". Read literally, "read only" suggests that every mutating
call on such a session fails.

**Resolution.** The guarantee this milestone requires is that a read-only
session can never **publish**: `merge`, `commit` and `rebase` must all report
the read-only category, and no read-only session may move a branch or write an
object. The per-node recorders (`add_group`, `add_array`, `update_array`, the
deletes, `set_user_attributes`, `set_chunk_ref`, `clear`) only mutate an
in-memory change set; whether they refuse on a read-only session is **not
scored in either direction**. The key-level writes on the store — `set`,
`set_if_not_exists`, `set_virtual_ref`, `delete` — *are* required to refuse,
because those are the calls a Zarr writer reaches for. `Store::clear`'s
behaviour on a read-only store is not scored either.
