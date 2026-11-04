# PROFILE.md — the benchmark's resolutions, milestone 07

The corpus beside this file is the Icechunk project's own documentation, copied
verbatim. It is written for users of the Python package, so it leaves some of
what this benchmark grades open, and occasionally describes something the Rust
crate expresses differently. This file records how those gaps are resolved.

Where this file and the corpus disagree, **this file wins**. Where this file and
`/app/TEST_INTERFACE.md` disagree, **`TEST_INTERFACE.md` wins**: it is the
interface contract, and this file only explains how the corpus maps onto it.

---

## 1. What is new in the corpus at this release

One document is new and nine changed, but only **three** of the nine carry a new
normative statement; the other six are documentation-link repairs. Of those:

| document | status at this release | carries a new normative statement? |
| --- | --- | --- |
| `guide/async.md` | **new**, 19 lines | yes — the asynchronous interface, the `_async` postfix rule, and the statement that the core is already fully asynchronous |
| `guide/configuration.md` | changed | yes — the new sentence that a second container is added, not substituted. (Its container examples were already separator-terminated at milestone 06; what moved here is the constructor's shape.) It does **not** say which container wins when two prefixes both match; see §3 |
| `guide/howto.md` | changed | yes — a new *Usage in async contexts* section |
| `guide/faq.md` | changed | yes — a new *Does Icechunk work with Zarr sharding?* section |
| `guide/virtual.md` | changed | no — one internal anchor was repointed; the prose is otherwise as it was |
| `guide/icechunk-for-git-users.md`, `guide/migration.md`, `guide/quickstart.md`, `guide/version-control.md`, `guide/xarray.md` | changed | no — cross-reference links only, repointed at the documentation site's new paths; not a word of prose moved |
| `spec.md` (and `guide/spec.md`) | **unchanged** | no |
| `guide/performance.md` | **unchanged** | no |
| `flatbuffers/*.fbs` | unchanged | no |
| `design/*` | unchanged | no |

So: **no new normative statement at this release comes from `spec.md` or
`performance.md`.** The on-disk format is untouched — no format-version bump, no
schema change — and a repository written by the previous checkpoint must open and
read identically here. Every new statement this release makes comes from
`async.md`, `configuration.md`, `howto.md`, `faq.md` and the project changelog.

## 2. The corpus is written for Python; the crate is Rust

The corpus's examples are Python. Four of this release's statements do not
translate directly, and the benchmark resolves them as follows.

**R7-1 — the asynchronous interface has no Rust counterpart.** `async.md` says
"the Icechunk core is fully asynchronous and delivers full parallelism and
performance whether you choose to use the synchronous or asynchronous
interface." In Rust that is a statement of existing fact, not a requirement:
every I/O method on `Repository`, `Session`, `Store`, `ops::gc` and `ops::stats`
has been an `async fn` since well before the previous checkpoint. The `_async`
postfix methods `async.md` documents are PyO3 wrappers in `icechunk-python`.
Build them; nothing in the Rust crate corresponds to them, and the verifier does
not reach them. See `TEST_INTERFACE.md` §5.

**R7-2 — `validate_container` has no default in Rust.** `configuration.md` and
the changelog describe container validation becoming the default when a virtual
reference is set. Rust has no default arguments: `validate_container` is a
positional, mandatory `bool` on `Store::set_virtual_ref` and
`Store::set_virtual_refs`, and was one at the previous checkpoint too. The
default lives in the Python wrapper. What this benchmark requires of the Rust
crate is the *behaviour the flag selects*, stated in `TEST_INTERFACE.md` §1.2.

**R7-3 — the cache warnings are logs, not values.** The changelog promises a log
warning and a remedy recommendation when a manifest or snapshot is too large for
its cache. In Rust this is a `tracing::warn!` from a private method: no return
value, no struct field, no error variant. Emit it; the benchmark does not grade
its text, because doing so would make a log message normative. What it does
grade is the configuration half — the raised defaults in `TEST_INTERFACE.md`
§1.6, and the fact that a tiny cache changes nothing about the data that comes
back.

**R7-4 — `ManifestFileInfo` accessors are a Python fix.** The changelog lists
property accessors on `ManifestFileInfo`. The Rust struct's three fields have
been `pub` since before the previous checkpoint and
`icechunk/src/format/snapshot.rs` is byte-identical across the two; the change
is `#[pyo3(get)]` on the Python wrapper. There is nothing to add in Rust.

## 3. The separator rule, which the corpus only shows by example

`configuration.md` writes every `VirtualChunkContainer` prefix with a trailing
separator (`"s3://my-s3-bucket/"`) but never states that the separator is
**required**. It is, at this release, and it is the highest-weighted thing the
verifier checks. Note that the file is not uniform about it: its own "Virtual
Chunk Credentials" example keys the authorisation map with an unseparated
prefix (`"s3://my_s3_bucket"`), which is precisely the persisted shape this
release must normalise rather than reject. The rule, its error, and what the resolver does with a prefix
or a credential key that lacks the separator are all stated in
`TEST_INTERFACE.md` §§1.1–1.2. The corpus's sentence that a second
`set_virtual_chunk_container` call adds a container rather than overwriting the
first still holds, and it is the reason the separator matters: matching is a raw
textual prefix test, so without the separator an authorisation for one prefix
reaches every sibling whose name extends it. The corpus does **not** say which
container wins when two prefixes both match a url; the tie-break — **the longest
prefix wins**, whatever order the containers were declared in — is stated only in
`TEST_INTERFACE.md` §1.2, and that is the authority for it.

## 4. Resolutions carried forward from earlier milestones

These were settled before this milestone and still apply. This overlay replaces
the earlier corpora under the same file names, so the resolutions **this
milestone's contract still depends on** are restated here; the rest of milestone
06's list is not repeated, because `/app/TEST_INTERFACE.md` §2 already carries
what survives of it. Where a resolution is not restated here and not in §2, it
has stopped mattering, not stopped being true.

**R1-1 — `spec.md` lags the implementation.** The normative specification
describes the on-disk format. It has never described the crate's Rust API, and
at several checkpoints it lagged the format itself. The Rust surface is governed
by each milestone's `TEST_INTERFACE.md`, never by `spec.md` alone.

**R1-2 — `archive/spec-alpha1.md` is historical.** It is the original long
specification. Where it and `spec.md` disagree, `spec.md` wins.

**R2-1 — the corpus documents the Python package.** Throughout the chain, a
statement in `guide/` describes `icechunk-python` unless it is plainly about the
format. When the two diverge, the Rust requirement is whatever
`TEST_INTERFACE.md` states.

**R3-1 — the file header is specified only in a design document.** The 39-byte
object header (magic bytes, client name, spec version, file type, compression
algorithm) introduced at milestone 03 is described in
`design/004-binary-format.md`, not in `spec.md`. What the benchmark requires of
it is the public `format::format_constants` surface, restated in this
milestone's `TEST_INTERFACE.md` §2.3.

**R4-1 — the FlatBuffers file identifier is written but not verified.**
Metadata buffers carry `Ichk` at bytes 4..8. Icechunk writes it and discriminates
on the header's file-type byte instead, so a buffer whose identifier is absent
is still accepted. The benchmark checks that the identifier is present on
written buffers; it does not require it to be rejected on read.

**R5-1 — the command-line interface is undocumented in the corpus.** No guide
document describes the `icechunk` binary. Its grammar, exit statuses and
configuration file are stated in `TEST_INTERFACE.md` §2.5, which is the sole
authority for them.

**R5-2 — chunk accounting sees materialised chunks only.** A chunk at or below
`inline_chunk_threshold_bytes` is stored inside its manifest and is not an
object, so it contributes nothing to `ops::stats::repo_chunks_storage` and
nothing to `GCSummary::chunks_deleted`. This is not a defect; it is why the
threshold must be lowered to measure chunk storage. Carried forward unchanged
into this milestone's `TEST_INTERFACE.md` §1.7 (this file has only §§1–6).

**R6-1 — "format 1.0" did not bump the version byte.** `SpecVersionBin::V0dot1`
is still `1`. "Format frozen as 1.0" is the project's compatibility policy,
stated in `guide/faq.md`, not a wire-format change. Still true here.

**R6-2 — the split-manifest design document is aspirational.**
`design/005-manifest-split.md` describes manifest sets, a maximum manifest size
and bin packing. None of that was implemented. The implemented model is the
chunk-space grid in `TEST_INTERFACE.md` §2.6: one manifest per non-empty cell.

**R6-3 — there are no default virtual chunk containers.** Milestone 06 removed
them, so a repository authorises virtual chunk access explicitly, by url prefix,
through the third argument of `Repository::{create, open, open_or_create}`. This
milestone builds directly on that: the authorisation map's keys are the thing
whose separator is now normalised.

**R6-4 — the client-name field follows the crate version.**
`format::format_constants::ICECHUNK_CLIENT_NAME` is `"ic-"` plus the crate
version, written space-padded into bytes 12–35 of every metadata object's
39-byte header (milestone 03, `design/004-binary-format.md`). This milestone
raises the crate from `0.3.0` to `0.3.4`, so that field's bytes change with it —
`ic-0.3.4`, not `ic-0.3.0` and never `ic-1.1.0`, which is the *Python* package's
version and the upstream tag. (`design/004-binary-format.md` writes
`'ic-1.0.0    '` in its worked example; that is the documentation illustrating
the field's width and padding, not the crate's value.) Nothing here asserts the
digits; what the format requires is the `"ic-"`-prefixed, space-padded 24-byte
field, and a milestone that raises the version must not also freeze the string.

## 5. The corpus carries two generations of the guide

Each milestone's corpus is copied **on top of** the previous one, and between
milestones 05 and 06 upstream moved its documentation out of
`icechunk-python/docs/` and renamed every file. So the tree you actually have
holds **55 files, `guide/` among them 34**, not the 42 and 21 in this
milestone's own overlay: the other thirteen are named
`guide/icechunk-python-*.md` and are the milestone 01–05 copies, left under
names nothing overwrites. `README.md` lists all thirteen.

**They are superseded in full and nothing in them is normative at this
milestone.** Where such a file and its unprefixed successor disagree, the
unprefixed file wins — and the successor is always the same name minus the
`icechunk-python-` prefix. The single exception is
`icechunk-python-developing.md`, a contributor guide the project dropped, which
has no successor and states nothing this milestone requires.

Of this milestone's own 42 files, **39 are listed in `SHA256SUMS`**; the other
three — this file, `README.md` and `SHA256SUMS` itself — are not, because the
first two are task-owned and the third cannot checksum itself. (`NOTICE` is
task-owned as well but *is* listed, so the 39 entries are 38 copied files plus
it; `NOTICE` names all four task-owned files.) The thirteen
superseded files are not listed either, since this milestone did not copy them.
That is deliberate, not an omission, and it is why the count in `SHA256SUMS`
(39) is smaller than the count of files on disk (55).

## 6. Things the corpus says that are *not* requirements here

* The MinIO, AWS, Google Cloud Storage, Azure, Tigris and Cloudflare R2
  instructions in `guide/storage.md` and `guide/virtual.md`. The verifier has no
  network and no object-storage service; only in-memory and local-filesystem
  storage, and `file://` virtual chunk references, are exercised.
* The Dask, Xarray and Zarr-sharding guidance in `guide/dask.md`,
  `guide/xarray.md`, `guide/parallel.md` and `guide/faq.md`. All of it is
  Python-level.
* The performance numbers in `guide/performance.md`. This milestone is not a
  performance task and no throughput figure is graded.
