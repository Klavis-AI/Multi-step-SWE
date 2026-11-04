# Benchmark compatibility profile

Task-owned resolutions where the Icechunk documents beside this file leave
behaviour open, describe a Python surface a Rust crate must express
differently, or have not caught up with the release this milestone is anchored
to. The documents in this directory are normative and unmodified; this file is
the only place the benchmark adds to them, and it never contradicts them.

Resolutions **P1–P9** restate, as they hold at *this* release, the principles
earlier milestones needed; **P10–P16** are new here. **D1–D4** carry forward
the previous milestone's documentation-drift corrections, every one of which
still applies because every document it names is byte-identical to the copy
that milestone shipped. The identifiers are local to this file.

---

## P1 — Language and boundary

The artifact is a **Rust** workspace. Every Icechunk document in this corpus
except `spec.md`, the FlatBuffers schemas and the design documents describes the
**Python** bindings. Read those for *semantics*; the Rust names, signatures and
module paths are fixed by the milestone's `TEST_INTERFACE.md`, never inferred
from a Python snippet. Where a Python method and a Rust function do the same
thing under different names, `TEST_INTERFACE.md` says which Rust item is scored.

`icechunk-python`, `icechunk-js`, Zarr-Python, Xarray and Dask are out of scope
at every milestone. Nothing in this task requires a Python interpreter.

## P2 — Storage backends in scope

Only two backends are ever exercised: **in-memory** and the **local
filesystem**. S3, Google Cloud Storage, Azure Blob, Cloudflare R2, Tigris and
MinIO are out of scope as running services — nothing in this task contacts a
network, reads a cloud credential, or needs a container. Their **constructors
and configuration types** are in scope where a milestone's `TEST_INTERFACE.md`
names them, and are exercised without any I/O.

## P3 — Error reporting

Failures are reported through the crate's `Result` types. The benchmark asserts
that an operation fails and, where `TEST_INTERFACE.md` names one, **which error
kind** it fails with. It never asserts an error message, a source chain, or the
text any `Display` or `Debug` impl produces. Error enums and their variants must
be public because the contract names them; their wording is yours.

Two things sit beside this rule rather than under it, and both are stated where
they apply: the **structural** properties of the command line's own standard
output (P12), and the requirement that a `Storage` rendering **not contain a
credential** (`TEST_INTERFACE.md` section 4.8) — a negative property, not an
assertion about wording.

## P4 — Undefined ordering

Where a document does not fix an order, none is required and none is asserted.
Where it does — the ancestry of a snapshot, newest first; the entries of a
manifest as written; chunk coordinates within an index — that order is
required.

## P5 — Timestamps

Generated timestamps are never compared to a wall clock. Only two things are
observed: their **ordering** relative to each other, and their comparison
against a **caller-supplied threshold** such as expiration's `older_than`. A
timestamp a document supplies must survive a round trip unchanged.

## P6 — Identifiers

Snapshot, manifest, chunk and node ids are opaque. The benchmark compares them
for equality, generates fresh ones, and carries them across API calls. It never
asserts a textual encoding, a length in characters, or an alphabet — except
inside the CLI's own output, where it asserts only that a line is the rendering
of a `SnapshotInfo` (P12).

## P7 — Internal layout

Module file names under `src/`, private types, caching strategy, allocation and
concurrency are yours. Only the module paths a milestone's `TEST_INTERFACE.md`
names are part of the public API.

## P8 — The candidate's own tests

The verifier compiles its own drivers against your crate and never runs, reads
or counts yours. Having no tests costs nothing; having failing ones costs
nothing.

## P9 — Versions

No scored group reads a version string out of a manifest. The crate version in
`TEST_INTERFACE.md` records which upstream release the milestone is anchored to
and is a build fact, not an assertion.

---

## P10 — This release adds no new normative statement to `spec.md`

`spec.md` in this directory is **byte-identical** to the previous milestone's
copy, and so is `guide/icechunk-python-configuration.md`. The on-disk format
did not change at this milestone: no new file kind, no new header, no new
field, no changed algorithm. Everything this milestone requires is either
behaviour over the existing format — expiration and collection accounting,
snapshot lookup, manifest information on a snapshot record — or a new surface
that lives outside the format, and is stated in `TEST_INTERFACE.md`.

Do not go looking in `spec.md` for a specification of this milestone's new
work. It is not there, and nothing in it changed.

## P11 — The command line is specified by the task, not by upstream prose

Upstream ships the `icechunk` executable but never documents it in its
published documentation: outside its changelog — which is **not part of this
corpus** — nothing upstream records more than that an executable exists, that
it is installed with the Python package, and that `--help` exits 0. Searching
this directory for the command line turns up nothing: no CLI reference, no man
page, no guide document, not one invocation.

Therefore `/app/TEST_INTERFACE.md` **section 5 is the normative specification
of the command line** for this task: the binary's name and feature gate, its
subcommands and options, its argument grammar, its exit statuses, the shape of
its standard output, and the location and syntax of its configuration file. It
does not contradict any document here; it fills a gap they leave.

## P12 — What is asserted about CLI output

A scored group runs the compiled binary as a process. It asserts:

- the **exit status**, per `TEST_INTERFACE.md` section 5.3;
- **which stream** carried the output (stdout for success and help, stderr for
  failures);
- **structural** properties of stdout: how many lines a listing has, that each
  line is the rendering of one `SnapshotInfo` and names the fields
  `TEST_INTERFACE.md` lists, that a configuration listing names each configured
  alias, its backend kind and its location, and that the newest snapshot comes
  first;
- that **help output names things**: `--help` names the four subcommands and
  `snapshot list --help` names `-n` and `--branch`, per `TEST_INTERFACE.md`
  section 5.4. Which words surround them is not asserted.

It does **not** assert layout, spacing, punctuation, field order, wording, or
any message text. A candidate is free to render a `SnapshotInfo` however its
`Debug` impl does. Nor does it assert that the *other* stream is empty: a
successful invocation may also write a log line to stderr and no group notices
(`TEST_INTERFACE.md` section 5.4).

The two **interactive** subcommands, `config init` and `config add`, need a
terminal. No scored group runs them, and nothing is asserted about their
prompts.

## P13 — "Total chunk storage" is a free function in Rust

The changelog entry this milestone inherits describes a
`Repository.total_chunks_storage` method. That name exists **only** in the
Python bindings. In Rust the same question is answered by the free function
`icechunk::ops::stats::repo_chunks_storage`, and that is the only thing scored.
A `Repository` method by any name is neither required nor forbidden.

It returns the bytes of **committed, native** chunk objects only: not
uncommitted ones, not inline ones, not virtual ones. Under the default
`inline_chunk_threshold_bytes` of 512, small chunks are inline and the function
legitimately returns `0` — set the threshold to a smaller value, or to `0`, to
make chunks into objects.

## P14 — There is no object storage-class configuration

Nothing at this release configures an object storage class: no such API exists
in the crate, and no document in this corpus mentions storage classes. The
"object-store-specific configuration" this milestone adds is exactly:
`S3Options::force_path_style`, the Tigris consistency mode and its region
requirement, the Cloudflare R2 constructor and its endpoint derivation, the
per-backend configuration maps, and the three `unsafe_use_*` switches on
`storage::Settings`. Nothing else, and nothing named "storage class", is
required or asserted.

## P15 — `RepositoryConfig` gains no field at this milestone

`RepositoryConfig` keeps exactly the fields it already had. New configuration
reaches a repository through its existing `storage: Option<storage::Settings>`
field, which is where this milestone's `unsafe_use_conditional_update`,
`unsafe_use_conditional_create` and `unsafe_use_metadata` switches live. Do not
add a field to `RepositoryConfig` expecting it to be scored; none is.

## P16 — Backend capability defaults, and a stale configuration document

**The three `unsafe_use_*` switches are documented nowhere in this corpus.**
`guide/icechunk-python-configuration.md`'s storage section — its heading is a
Markdown link at line 54, `### [storage](./reference.md#icechunk.RepositoryConfig.storage)`,
not a bare `### storage` — still says at line 56 of the repository's storage
settings that *"Currently, the only setting available is the concurrency
settings"* — a sentence this release has outgrown, since
`StorageSettings` now also carries `unsafe_use_conditional_update`,
`unsafe_use_conditional_create` and `unsafe_use_metadata`. Read that section
for the shape of the `storage` field, not for its contents, and take the
contents from `/app/TEST_INTERFACE.md` section 4.8, which is normative for
them.

Nor does any document say **who** decides a switch's value. This benchmark
resolves it: **a backend declares in its own `default_settings()` the
capabilities it cannot honour.** Concretely, the local-filesystem backend
reports `unsafe_use_conditional_update() == false` and
`unsafe_use_metadata() == false`, while the in-memory backend leaves both on. A
`RepositoryConfig::storage` value supplied by the caller overrides the
backend's defaults for that repository.

That is a rule about the switches a backend **cannot** honour, not about
conditional operations in general. Conditional *create* a local filesystem can
honour, and no backend at this release turns it off, so the local-filesystem
backend leaves `unsafe_use_conditional_create` unset. Its
`default_settings().unsafe_use_conditional_create()` therefore reports
`true`, and so does the same accessor on `Repository::storage_settings()`
for a repository built over that backend with no `storage` configured.
`/app/TEST_INTERFACE.md` section 4.8 is normative for all three switches.

The same principle decides the Tigris consistency mode: the weakly consistent
mode is read-only, so its backend reports `can_write() == false`.

---

## Documentation drift carried forward

Four places where a reader who follows the corpus literally will disagree with
the code. Each was resolved at the previous milestone and each still applies
here unchanged: every file named below is byte-identical to that milestone's
copy, and nothing at this release revisits the behaviour. In each, the
resolution below governs, and `/app/TEST_INTERFACE.md` states the required
behaviour.

### D1 — `spec.md` still describes sequence-numbered branch files

`spec.md` `#### Creating and Updating Branches` (line 160) describes a
per-branch *sequence number*, encoded as `1099511627775 - n` in Base 32
Crockford, left-padded to 8 characters, giving file names such as
`refs/branch.main/ZZZZZZZZ.json` and a ceiling of `1099511627775` commits, with
the tip found by listing the prefix and taking the lexicographically first
entry.

**That is no longer true.** A branch is a *single* file,
`refs/branch.$BRANCH_NAME/ref.json`, updated with a **conditional put** (ETag
or generation) rather than a create-if-not-exists on a new name. There is no
sequence number, no encoding, no commit ceiling, and no prefix listing on the
read path.

Everything the section says about *why* — optimistic concurrency, exactly one
writer winning, the loser retrying or failing — still holds; only the mechanism
changed. The surrounding sections (`### Reference Files`, "Tags") are accurate,
which is why `instruction.md` section 2 still sends you to them.

### D2 — `design/004-binary-format.md` still says the payload is Msgpack

That document's "### File structure" table ends with a Msgpack row and the
prose above it says the bytes after the header are "the compressed Msgpack
serialization of the corresponding Rust datastructure".

**The payload is a zstd frame whose plaintext is a FlatBuffers root**, per
`design/008-no-copy-serialization-formats.md` and the schemas in
`flatbuffers/`. Everything else in that document is accurate: the 12 magic
bytes, the 24-byte implementation id padded with ASCII spaces, byte 36 as the
spec version, byte 37 as the file type, byte 38 as the compression algorithm,
and compression applying only after the 39-byte header — which is what that
document's own "### File structure" table says. Its prose one paragraph above
that table instead says compression applies "to bytes after and including index
27"; the table is the correct one. Nothing at this milestone reads a file
header, so neither number is scored here.

### D3 — `transaction_log.fbs` has two swapped comments

In `flatbuffers/transaction_log.fbs`, the comment above `updated_arrays` says
"groups" and the comment above `updated_groups` says "arrays". The **field
names** are correct and are what the writer and the reader match on; the
comments are swapped. There is no runtime effect, and no implementation should
follow the comments.

### D4 — `"Ichk"` is written but never verified

None of the five `.fbs` files declares a `file_identifier`. The writer still
passes `"Ichk"` when finishing the buffer, so those four bytes are present at
offset 4 of every snapshot, manifest and transaction-log buffer — identically
for all three types. But because the schemas declare no identifier, nothing
checks those bytes on read: a buffer whose bytes 4..8 have been overwritten
still verifies and still decodes.
