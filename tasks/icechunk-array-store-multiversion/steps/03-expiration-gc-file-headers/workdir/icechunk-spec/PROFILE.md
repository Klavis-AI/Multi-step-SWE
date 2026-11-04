# Benchmark compatibility profile — milestone 03

Task-owned resolutions where the Icechunk documents beside this file leave
behaviour open, narrow it deliberately, or have fallen behind the release they
ship with. The documents are normative and unmodified; this file is the only
place the benchmark adds to them, and where it overrides one it says so and
says why.

Sections `P*` are standing resolutions carried from earlier milestones, in
their milestone-02 numbering, with any change this release forces stated in
place. Sections `R*` record an earlier resolution this release **retires**.
Sections `D*` are the drifts a reader of *this* release's corpus must know
about — between a document and the implementation, or between the corpus as
published and the directory as you find it; each names the drift and says which
side to follow.

---

## Standing resolutions carried forward from milestone 02

### P1 — Storage backends in scope

The only storage backends exercised are **in-memory** and the **local
filesystem** (`new_in_memory_storage` / `ObjectStorage::new_in_memory`, and
`new_local_filesystem_storage` / `ObjectStorage::new_local_filesystem`). S3,
GCS, Azure and Tigris configuration types must be constructible and must
serialise, but no request is ever made to them, no credential is ever supplied
that reaches a network, and nothing in this task requires MinIO, Azurite, a
cloud account or a metastore. `spec.md` → "Storage Operations" lists the
capabilities a backend must have; those capabilities are observed through the
two local backends only.

A consequence new at this milestone: `Storage::fetch_manifest_known_size` splits
one object read into several byte-range requests, and that is only interesting
against a remote store. The local backends serve a single read, so parallel
fetching is not observed.

### P2 — The Zarr array format is not in scope

Icechunk stores Zarr arrays; it does not define them. Codecs, data-type
semantics, fill-value encoding and chunk compression are never exercised. Chunk
bytes are carried opaque, and this milestone's headline requirement about them
is precisely that: what Zarr wrote is what the store holds, byte for byte
(`design/004-binary-format.md` → "## Chunk files").

`spec.md` → "### Chunk Files" additionally permits a chunk file to hold several
contiguous chunks, chunks from several arrays, or a foreign file's contents.
**None of that is produced or required here**: a materialised chunk reference
points at an object written for it alone, which is what makes counting chunk
objects a meaningful observation of garbage collection. A reader must still
accept an arbitrary `offset` and `length` in a `ChunkRef`, because a virtual
reference uses exactly that shape against a foreign file.

### P3 — Object serialisation formats — **amended at this milestone**

Milestone 02's resolution read: the benchmark never opens an object-store key
directly and never asserts an encoding, because `spec.md` → "File Formats"
declares the encodings to be in flux.

**That no longer holds.** `design/004-binary-format.md` fixes a byte layout for
metadata files, so at this milestone the benchmark **does** read raw objects and
**does** assert bytes — but only the bytes that document fixes: the 12 magic
bytes, the 24-byte implementation id, the spec-version, file-type and
compression bytes, and that the payload behind them is a Zstandard frame
starting at byte 39. Nothing asserts the MessagePack encoding inside that frame,
the internal key names of any serialised structure, or the object-store key a
file is stored at beyond the prefixes of §D4.

### P4 — Undefined ordering

Where a document fixes no order, none is required and none is asserted: the
order of branches and tags in a listing, of nodes, of chunks, of the coordinates
`Session::chunk_coordinates` yields, of the `Storage::list_*` streams, and of
the references expiration processes. Every such collection is sorted or compared
as a set. Where a document *does* fix an order, it is required: ancestry is
newest first, `rebase` fast-forwards oldest first, branch reference files sort
with the newest sequence first, and `list_branches` / `list_tags` return sorted
sets.

### P5 — Timestamps — **amended at this milestone**

Milestone 02's resolution read: a snapshot's `written_at` is never compared to a
wall clock, only checked to exist and survive a round trip.

The field is now spelled **`flushed_at`** (§D6), and this milestone observes
more of it, though still never against a wall clock. What is asserted is
relational only: that a snapshot's timestamp is strictly greater than its
parent's, and that expiration with a cutoff taken *from an observed snapshot's
own timestamp* keeps exactly the snapshots at or after it. No absolute value and
no elapsed duration is ever asserted, and no group sleeps or races a clock.

### P6 — Error reporting

Failures are reported through the crate's `Result` aliases. The benchmark
asserts the **category** an operation fails with — the error enum variant
`TEST_INTERFACE.md` names — and never a message, a `Display` or `Debug`
rendering, or a source chain. Where `TEST_INTERFACE.md` names no category, only
that the operation fails is asserted.

### P7 — The inline-chunk threshold is policy, not format

No specification document states a size at which a chunk is stored inline rather
than materialised. It is a configuration value
(`inline_chunk_threshold_bytes`, default 512), applied as *strictly larger than*
the threshold materialises and *at or below* it inlines. A threshold of `0`
therefore materialises every non-empty chunk, and that is how this milestone's
garbage-collection groups arrange for chunk objects to exist. A reader must
accept all three reference kinds regardless of the threshold in force.

### P8 — Attributes files stay unimplemented

`spec.md` → "Attributes Files" still carries its explicit warning that attribute
files have not been implemented, and they are still not implemented at this
release. Out-of-line user attributes remain out of scope. The `attributes`
category exists in the garbage-collection configuration and summary and must be
present and wired, but its count is always zero.

### P9 — Unsafe reference overwriting

`unsafe_overwrite_refs` (default `false`) deliberately turns a conditional
create-if-not-exists into an unconditional overwrite, for stores without that
primitive. The benchmark only ever exercises the default, so the guarantee that
exactly one of two concurrent commits to a branch succeeds is required — and so
is tag immutability, which rests on the same conditional write.

---

## Earlier resolutions this release retires

### R1 — Configuration override is now per field, not wholesale

Milestone 02's profile (its §D3) resolved a disagreement between two design
documents in favour of `design/003-python-instantiation-api.md`: supplying a
configuration when a repository is opened replaced the persisted one entirely,
and "per-field merging is a later milestone's capability".

**This is that milestone, and the resolution is retired.** Every
`RepositoryConfig` field is now `Option`, read through a same-named getter, and
a supplied configuration is **merged** over the persisted one field by field —
recursively, into `compression`, `caching`, `storage` and `manifest`. A field
the caller did not set takes the persisted value, and then the built-in default.
The other half of `design/001-virtual-chunk-improvements.md` → "Global config"
is unchanged: one configuration per repository, written with a conditional
write. `TEST_INTERFACE.md` section 9.4 gives the getters and every default.

The virtual chunk container map is the one field that does **not** merge
element-wise: a caller who supplies `virtual_chunk_containers` replaces the
built-in default set for that repository rather than adding to it, which is what
milestone 02's §D5 meant by "a repository may replace or clear the default set".

### R2 — Tags can be deleted

Milestone 02 inherited `spec.md`'s statement that tags are immutable and
undeletable, and required it. `design/006-tag-delete.md` retires the second half
of that: tag deletion exists at this release. Immutability itself is **not**
retired — a live tag still cannot be moved, creating one that already exists
still fails, and the deleted name can never be reused, which is exactly how the
guarantee survives deletion. See §D2.

### R3 — There is now a defined on-disk file format for metadata files

Milestone 02 followed `spec.md` → "File Formats" in treating the encodings as
undefined. `design/004-binary-format.md` defines them. See §P3 and §D1.

---

## Drifts in this release's own corpus

### D1 — The binary file header is specified only in a design document

**The drift.** `spec.md` at this release is **byte-identical to milestone 02's**.
Its "### File Formats" section still warns that the formats "are in flux" and
describes data structures rather than a byte layout. It says **nothing** about a
file header, even though every metadata file this release writes now carries
one.

**Resolution.** `design/004-binary-format.md` — its "## Metadata files",
"### File structure" and "## Chunk files" — is the normative statement of the
header for this milestone. Where `spec.md` is silent, that document is the
specification; where they overlap (metadata files are MessagePack), both hold.

Two details `design/004` states loosely, fixed here:

- Its example implementation ids are `"ic-0.1.0-a.8"` and `"ic-1.0.0"`. The id
  required at this milestone is `"ic-"` followed by the crate version, so
  `"ic-0.1.0"`, right-padded with ASCII spaces to exactly 24 bytes.
- Its prose says compression "doesn't apply to the header, only to bytes after
  and including index 27". The table in the same section is authoritative: the
  header is bytes **0..39** and the compressed payload begins at byte **39**.
  `TEST_INTERFACE.md` section 5.1 states the layout that is scored.

`design/004` also records that on object stores that support it the same
information is written as object-store metadata but is not consulted on read.
Writing it is permitted and expected; nothing in this task reads it back, and
the two backends of §P1 do not both support it.

### D2 — `spec.md` still says tags cannot be deleted

**The drift.** `spec.md` → "#### Tags" ends with "Tags cannot be deleted once
created." That was true at milestone 02 and is false at this one.

**Resolution.** Follow `design/006-tag-delete.md`, corroborated by
`guide/icechunk-python-version-control.md` → "## Tags" ("once a tag is deleted,
it can never be recreated"). Deletion is logical: a tombstone is written beside
the live reference, resolution consults both, a deleted tag disappears from
listings, and the name is permanently unusable. See §R2.

### D3 — `spec.md`'s embedded Rust structs are illustrative and stale

**The drift.** `spec.md` → "### Snapshot Files" and "### Chunk Manifest Files"
quote the library's internal `Snapshot` and `Manifest` structs, each prefixed by
a warning that the structure "will most certainly change". Both quotations are
from an earlier release: `Snapshot`'s fields are now private behind accessors,
`Manifest` is keyed per node and carries an `id`, and the
`icechunk_*_format_version` / `_format_flags` members are gone, replaced by the
header of §D1.

**Resolution.** Those code blocks are **not** part of the required interface;
`TEST_INTERFACE.md` sections 8.2 and 9.2 are. What the blocks still fix, and
what remains required, is the semantic content: a snapshot fully describes the
repository's schema and points at its manifests, and a manifest holds chunk
references of exactly three kinds — `Ref`, `Inline` and `Virtual`.

### D4 — `spec.md`'s file layout omits two prefixes the implementation uses

**The drift.** `spec.md` → "### File Layout" lists `$ROOT/refs/`,
`$ROOT/snapshots/`, `$ROOT/attributes/`, `$ROOT/manifests/` and `$ROOT/chunks/`,
and nothing else.

**Resolution.** Two more locations are in scope and required:

- `$ROOT/transactions/` — one transaction-log object per commit, named by the
  snapshot id. Transaction logs are what `rebase` reads; they carry the header
  of §D1 with file type `0x04`, and garbage collection counts them separately.
- `$ROOT/config.yaml` — the persisted repository configuration
  (`design/001-virtual-chunk-improvements.md` → "### Global config").

`$ROOT/attributes/` is listed by `spec.md` but nothing is ever written there
(§P8).

### D5 — The configuration guide's defaults section was deleted

**The drift.** `guide/icechunk-python-configuration.md` → "## Repository Config"
is now one sentence of introduction followed by a note reading "This section is
under construction and coming soon", and nothing else. Milestone 02 could read
the inline-chunk default out of that page's "### Writing chunks inline" section;
at this release that section has been **removed** along with "### Virtual
Reference Storage Config".

**Resolution.** Every configuration default is stated in `TEST_INTERFACE.md`
section 9.4 instead, and that is the authority for them at this milestone. The
same guide's surviving sections — "### Creating a new repo", "### Opening an
existing repo" — are current and are followed, and the first of them is the
prose source for the clean-prefix rule.

### D6 — The Python guide prints `written_at`; the Rust field is `flushed_at`

**The drift.** `guide/icechunk-python-version-control.md` → "## Setup" prints
`SnapshotInfo(id=…, parent_id=…, written_at=…, message=…)`.

**Resolution.** That is the Python binding's field name. The Rust field this
milestone requires is **`flushed_at`**, renamed from milestone 02's
`SnapshotMetadata::written_at`. Where a Python name and a Rust name differ, the
Rust name is what is scored; `icechunk-python` is not built (§D9).

### D7 — The clock-drift rule is not observable from outside the crate

**The drift.** `design/007-basic-expiration.md` → "## On timestamps" asks for
two guards: monotonic snapshot timestamps, and interrupting a flush when the
snapshot's timestamp and the object store's `last-updated-at` differ by more
than a few minutes.

**Resolution.** The first is required and is observed (§P5). The second is
**out of scope**: it compares against `Storage::get_snapshot_last_modified`,
which on both backends of §P1 is the same machine clock, and the `Storage` trait
is sealed, so no test outside the crate can supply a skewed one. The shipped
limit is 600 seconds; implementing it is correct and harmless, and nothing
asserts it either way.

The same section's third rule — an in-place snapshot update during expiration
keeps the snapshot's original timestamp and ignores the object store's value —
is required, and follows from the ancestry staying well-formed afterwards.

### D8 — Concurrent expiration is undefined

**The drift / resolution.** `design/007-basic-expiration.md` →
"### Comparison between the algorithms" explicitly leaves the behaviour of two
expiration operations running at once undefined, and warns that any operation
making several passes over history while expiration runs may see different
histories per pass. The benchmark never runs expiration concurrently with
anything and asserts nothing about that case. What it does require is the
document's other concurrency statement: a reader of a snapshot being rewritten
sees either the pre- or the post-expiration history, never an invalid one, which
follows from rewriting a single object in place.

### D9 — Python-only behaviour

**The drift / resolution.** The opt-in required before pickling a writable
session (`guide/icechunk-python-parallel.md` → "## Distributed writes",
`guide/icechunk-python-faq.md`) lives in the Python bindings, not in the Rust
crate. `icechunk-python` is not built and not scored. The Rust
`Session::as_bytes` / `Session::from_bytes` round trip is public and must keep
working, but no group asserts it at this milestone.

### D10 — `design/005`'s manifest sets are a plan, not this release

**The drift.** `design/005-manifest-split.md` → "## Design" describes far more
than this release ships: *manifest sets*, per-array assignment *rules*, the
whole `chunk-manifests:` YAML block with `max-manifest-size`,
`arrays-per-manifest`, `overflow-to` and `cardinality`, the bin-packing
algorithm, and a **default configuration** whose single rule
(`metadata-chunks: [0, 5000]`, `path: .*`, `target: coordinates`,
`cardinality: 1`) would put every array under 5,000 chunks into **one shared
manifest**. Read literally, that default is the opposite of one manifest per
array.

**Resolution.** None of the manifest-set machinery exists at this release, and
none of it is required or scored. What `design/005` actually contributes here is
the opening of its own "## Design" section — "We don't implement array
splitting at this time. Each array will go fully into a single manifest" (quoted
without its author's emphasis, because it has none) — plus the bounding box that
"## Current state" says was unimplemented and now is not. So:

- **one manifest object per array per commit**, each named by exactly one
  `ManifestRef` on that array's node, with `extents` the end-exclusive
  chunk-space bounding box. `TEST_INTERFACE.md` section 9.2 is normative for
  this, and `instruction.md` section 1 says the same thing;
- the surviving configuration surface is `ManifestConfig`,
  `ManifestPreloadConfig` and `ManifestPreloadCondition`
  (`TEST_INTERFACE.md` section 9.4). They must exist, carry their defaults and
  merge per field; their *effect* is not scored (section 12);
- the YAML above is **not** a configuration format this release parses, the
  packing algorithm is not implemented, and no group asserts anything from
  "### Algorithm", "#### Packing by …" or "### How `flush` changes".

The document's "## Discussion" and "## Design" motivation — why coordinate
arrays want to be packed together — is background, not a requirement.

### D11 — the corpus at `/app` carries one superseded guide page

**The drift.** Each milestone's `icechunk-spec/` is *overlaid* on the previous
milestone's rather than replacing it, and an overlay writes same-named files but
deletes none. Upstream dropped `guide/icechunk-python-developing.md` after
milestone 01, so the copy milestone 01 shipped is still in `/app` even though
this release's corpus does not contain it. Everything else in `/app/icechunk-spec/`
is this release's.

**Resolution.** `guide/icechunk-python-developing.md` is **superseded and
non-normative here**: 80 lines on building and testing the Python package with
`maturin`, stating nothing about the format or the Rust API. It is not listed
in this directory's `SHA256SUMS`, which covers only the 27 files this milestone
copied. `README.md` describes the merged tree a fresh reader actually sees: 31
files in `icechunk-spec/`, 17 of them in `guide/`.

The same applies at every later milestone: where a document's name changed or a
document was dropped upstream, the older file may survive on disk, and only the
files a milestone's own `SHA256SUMS` lists are that milestone's corpus.
