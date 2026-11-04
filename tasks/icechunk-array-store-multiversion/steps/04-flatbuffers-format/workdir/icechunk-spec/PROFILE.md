# Benchmark compatibility profile — milestone 04 (`v0.2.0`)

Task-owned resolutions where the Icechunk corpus beside this file leaves
behaviour open, is stale, or contradicts itself. The corpus documents are
verbatim and unmodified; **this file is the only place the benchmark adds to
them**, and `/app/TEST_INTERFACE.md` is where the resulting contract is
written down in full.

This file is cumulative. Sections **P1–P8** carry forward the resolutions
earlier milestones needed and restate them as they hold *at this release*;
sections **P9–P13** and **D1–D4** are new here. The identifiers are local to
this file.

---

## Carried forward

### P1 — Object identifiers are 12 bytes (8 for nodes)

`archive/spec-alpha1.md` says an `ObjectId` is an array of 16 `uint8`. It is
not, and never was: snapshot, manifest, chunk and attributes ids are **12
bytes**, node ids are **8 bytes**. `spec.md` at this release agrees with the
code — "The snapshot id is 12 byte random binary, so the encoded string has 20
characters" — and `flatbuffers/object_ids.fbs` fixes both sizes in the schema.
The 12/8 split is what this benchmark requires.

Ids are spelled as **Base 32 Crockford** wherever they appear as text: 20
characters for a 12-byte id, and the spelling round-trips back to the same
bytes.

### P2 — The object-store key layout

`archive/spec-alpha1.md` says `$ROOT/r/ s/ a/ m/ c/`. That is not what any
release does. `spec.md` "File Layout" at this release is correct and is what
this benchmark requires:

```text
$ROOT/refs/          reference files
$ROOT/snapshots/     snapshot files
$ROOT/attributes/    attribute files   (never written - see P5)
$ROOT/manifests/     chunk manifests
$ROOT/transactions/  transaction log files
$ROOT/chunks/        chunks
```

### P3 — The reference-file grammar

`archive/spec-alpha1.md` requires a reference JSON object with `properties`,
`snapshot` and `timestamp`, and puts tags at `r/$TAG_NAME.json`. Neither holds.
At this release:

- a reference file is a JSON object with the **single** key `"snapshot"`, whose
  value is the Base 32 Crockford spelling of the snapshot id, exactly as
  `spec.md` "Reference Files" shows;
- a **tag** is `refs/tag.$TAG_NAME/ref.json` — unchanged since the first
  release, and unchanged by this one;
- a **branch** is `refs/branch.$BRANCH_NAME/ref.json` — see D1, this is the
  change this release makes;
- branch and tag names may not contain `/`;
- the default branch is `main`, and whether `refs/branch.main/ref.json`
  resolves is the **whole** definition of "this prefix holds a repository".

### P4 — Tag deletion

`archive/spec-alpha1.md` says tags can never be deleted. Since the milestone
that introduced `design/006-tag-delete.md`, they can: deletion writes a
tombstone beside the tag, the tag stops resolving and stops being listed, and
**the name can never be reused**. That is the behaviour this benchmark
requires; tag *mutation* remains impossible.

### P5 — Attributes files

`spec.md` "Attributes Files" says the on-disk format "has not been defined in
full yet" and that attributes files "have not been implemented". Nothing in
this benchmark writes, reads or scores an attributes file. `FileTypeBin`
reserves the code `3` for them and that code must not be reused.

User attributes live inside the node's own metadata bytes — see P10.

### P6 — The inline-chunk threshold is policy, not specification

No document states a size below which a chunk is stored inline. It is
configuration: `RepositoryConfig::inline_chunk_threshold_bytes()` defaults to
**512**, and the rule is `data.len() > threshold` — so a payload of exactly 512
bytes is still inlined. A chunk above the threshold is written as its own
object and referenced natively.

### P7 — No network, no cloud, no object-store emulator

S3, GCS, Azure and Tigris are configuration surfaces in this benchmark, never
runtime dependencies. Nothing scored opens a network connection, reaches MinIO,
Azurite or a public bucket, or needs credentials. Everything runs on the
in-memory object store or a local-filesystem store in a temporary directory,
and virtual chunk references use `file://` URLs.

### P8 — Errors are asserted by category

Failures are observed through the crate's own error types, matched by
**category**: the alias's `kind` field against a named variant of the
corresponding `…ErrorKind`, occasionally with a named field's value. Message
wording, `Display` text, `Debug` rendering and the source chain beyond
"a source exists" are never asserted.

---

## New at this release

### P9 — The byte layout of a metadata file is normative here

`spec.md` "File Formats" carries a standing warning that "the actual file
formats used for each type of metadata file are in flux" and that the spec
"describes the data structures encoded in these files, rather than a specific
file format". This benchmark narrows that: at this release the envelope **is**
fixed, and `/app/TEST_INTERFACE.md` section 5 states it byte for byte — a
39-byte plaintext header followed by a compressed frame whose plaintext is the
bare FlatBuffers root. `design/004-binary-format.md` is the corpus source for
the header (with the correction in D2).

### P10 — Zarr metadata is opaque bytes

This release deletes Icechunk's model of Zarr array metadata. A node carries
the caller's metadata **verbatim** as bytes, plus a typed `shape` and optional
`dimension_names` that Icechunk does parse out of it. Everything else in the
document — `data_type`, `fill_value`, `codecs`, `storage_transformers`,
`chunk_key_encoding`, `attributes`, and any extension key Zarr does not define
— is stored and returned unchanged, byte for byte, before and after a commit.
That is what "Icechunk is resilient to changes in the Zarr metadata spec and
can handle Zarr extensions" means here, and it is what is scored.

Consequence: there is no validation of Zarr metadata beyond the few fields
above, and no scored check requires Icechunk to reject a metadata document that
Zarr itself would reject.

### P11 — There is no migration from the previous on-disk format

The format change at this release is not backwards compatible and no migration
path exists. Data written by an earlier release must be re-ingested. What *is*
required is that the rejection be **clean and named**, at both levels — see
`/app/TEST_INTERFACE.md` section 5.4.

### P12 — The Python binding is out of scope

The corpus's prose guides are written for the Python API. Only the Rust crate
is built and graded. Where a release-note item or a guide describes a
Python-only surface — a keyword-argument spelling, an iterator type, a wrapper
class — it is out of scope; `/app/TEST_INTERFACE.md` section 10 names the two
that matter at this release.

### P13 — Leftover documents in the corpus tree are not normative

`/app` is cumulative and each milestone's corpus is **merged** into
`/app/icechunk-spec/` rather than swapping out the previous one, so a document
upstream renamed or dropped between two releases stays behind under its old
name. At this milestone exactly one file is in that position:
`guide/icechunk-python-developing.md`, carried over from milestone 01.

The test is mechanical: **`SHA256SUMS` lists every file this milestone copied
and nothing else**. So a file under `icechunk-spec/` that `SHA256SUMS` does not
list is either one of the three the checksum file cannot list — itself, and the
two task-owned documents `README.md` and `PROFILE.md` — or a leftover from an
earlier milestone. A leftover is not normative here: read the file this release
actually ships instead. It does not break the integrity check either, which
verifies the files it lists and ignores an unlisted extra.

---

## Documentation drift at this release

Four places where a reader who follows the corpus literally will disagree with
the code. In each, the resolution below governs, and
`/app/TEST_INTERFACE.md` states the required behaviour.

### D1 — `spec.md` still describes sequence-numbered branch files

`spec.md` "#### Creating and Updating Branches" describes a per-branch
*sequence number*, encoded as `1099511627775 - n` in Base 32 Crockford,
left-padded to 8 characters, giving file names such as
`refs/branch.main/ZZZZZZZZ.json` and a ceiling of `1099511627775` commits, with
the tip found by listing the prefix and taking the lexicographically first
entry.

**That is no longer true at this release.** A branch is a *single* file,
`refs/branch.$BRANCH_NAME/ref.json`, updated with a **conditional put**
(ETag or generation) rather than a create-if-not-exists on a new name. There is
no sequence number, no encoding, no commit ceiling, and no prefix listing on the
read path.

Everything the section says about *why* — optimistic concurrency, exactly one
writer winning, the loser retrying or failing — still holds; only the mechanism
changed. The surrounding sections ("Reference Files", "Tags") are accurate.

This is also the reason a repository written by the previous release cannot be
opened by this one: it has no `ref.json`.

### D2 — `design/004-binary-format.md` still says the payload is Msgpack

The document's "### File structure" table ends with

> | 39..end | Msgpack serializanion | |

and the prose above it says "The following bytes are the compressed Msgpack
serialization of the corresponding Rust datastructure."

**At this release the payload is a zstd frame whose plaintext is a FlatBuffers
root**, per `design/008-no-copy-serialization-formats.md` and the schemas in
`flatbuffers/`. Everything else in that document is accurate and normative: the
12 magic bytes and their hex spelling, the 24-byte implementation id padded with
ASCII spaces, byte 36 as the spec version, byte 37 as the file type, byte 38 as
the compression algorithm, and the fact that compression applies only after the
header. (The prose says compression starts at "index 27"; the byte table is
right and the prose is a typo — the header is 39 bytes and the payload starts at
index 39.)

### D3 — `transaction_log.fbs` has two swapped comments

In `flatbuffers/transaction_log.fbs`, the comment above `updated_arrays` says
"groups" and the comment above `updated_groups` says "arrays". The **field
names** are correct and are what the writer and the reader match on; the
comments are swapped. There is no runtime effect, and no implementation should
follow the comments.

### D4 — `"Ichk"` is written but never verified

None of the five `.fbs` files declares a `file_identifier`. The writer still
passes `"Ichk"` when finishing the buffer, so those four bytes are present at
offset 4 of every snapshot, manifest and transaction-log buffer — **identically
for all three types**. But because the schemas declare no identifier,
`flatc` emits no `root_as_*` helper and no `*_IDENTIFIER` constant, and nothing
checks those bytes on read: a buffer whose bytes 4..8 have been overwritten
still verifies and still decodes.

File-type discrimination is therefore done **entirely by header byte 37**, and
what stops a manifest being read as a snapshot is the structural FlatBuffers
verification, not the identifier. A reader that wants to check `"Ichk"` must
compare `buffer[4..8]` itself.
