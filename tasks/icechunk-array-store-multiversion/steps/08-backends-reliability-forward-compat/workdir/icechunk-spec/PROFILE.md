# PROFILE.md — the benchmark's resolutions, milestone 08

The corpus beside this file is the Icechunk project's own documentation, copied
verbatim. It is written for users of the Python package, so it leaves some of
what this benchmark grades open, and occasionally describes something the Rust
crate expresses differently — or something the code does not do at all.

Where this file and the corpus disagree, **this file wins**. Where this file and
`/app/TEST_INTERFACE.md` disagree, **`TEST_INTERFACE.md` wins**: it is the
interface contract, and this file only explains how the corpus maps onto it.

---

## 1. What is new in the corpus at this release

Four documents are new and six changed.

| document | status | carries a new normative statement? |
| --- | --- | --- |
| `guide/version-policy.md` | **new** | yes — the single-integer on-disk version, the major-version correspondence, and the refusal to read forward |
| `guide/concepts.md` | **new** | no — it consolidates the repository/session/snapshot model milestones 01–03 already established |
| `design/010-notes-towards-an-IC-2.md` | **new** | no — motivation only |
| `design/011-ref-and-ancestry-entry-point.md` | **new** | no — a design **not implemented** at this release; see §R8-3 |
| `guide/configuration.md` | changed | yes — `max_concurrent_requests`, and the whole `## Proxy configuration` section |
| `guide/performance.md` | changed | yes — the whole `## Concurrency` block, including the global limit's default of 256 and stalled-stream detection |
| `guide/virtual.md` | changed | no — a security admonition restating milestone 06's authorisation rule |
| `guide/faq.md`, `guide/overview.md`, `guide/icechunk-for-git-users.md` | changed | no — prose rewordings of existing behaviour |
| `spec.md` (and `guide/spec.md`) | **unchanged** | no |
| `guide/storage.md` | **unchanged** | no |
| `flatbuffers/*.fbs` | unchanged | no |

So: **the on-disk format is untouched at this release.** No format-version bump,
no schema change, and a repository written by the previous milestone opens and
reads identically here. What changed is the *library's* behaviour when it meets
a repository it was not built for.

## 2. Contradictions in the corpus, and how they are resolved

Six, plus one duplication the overlay chain creates. Each is resolved in favour
of the code, and the verifier was written against the resolution. The six are
**R8-1** to **R8-6**; the duplication is **R8-13**, and it closes this section
out of numeric order on purpose — R8-7 to R8-12, in §§4–5, are the
change-log-versus-Rust readings and were numbered before it.

> **A note on the citations below.** Several resolutions here and in §§4–5 say
> what "the project's own change log" records for this release. That is the
> upstream release note for the version you are implementing, and it is **not in
> this workspace**: `/app/Changelog.md` and `/app/Changelog.python.md` stop at
> the *previous* milestone, and writing this release's entry is no part of what
> is graded. The citation is there to say where the resolution came from, never
> to send you looking. Every resolution states its own outcome in the same
> sentence, and `/app/TEST_INTERFACE.md` is the authority for all of them.

**R8-1 — the stalled-stream default is stated wrongly.**
`guide/performance.md`, `### Stalled Network Streams`, says "The default is 60
seconds" for `network_stream_timeout_seconds`. The project's own change log says
the default was **reduced to 10 seconds** to match the AWS SDK, and the code
agrees with the change log: the grace period is
`network_stream_timeout_seconds.unwrap_or(10)` seconds. **Ten seconds is the
resolution.** `Some(0)` disables stall detection entirely, as the document says.
See `TEST_INTERFACE.md` §10. Note that the detection *behaviour* is not graded
at all — only the configuration field — so this resolution matters for what you
implement, not for what is measured.

**R8-2 — Azure is described as unfinished in two places and finished in a
third.** `guide/virtual.md` still says Azure virtual-chunk support "is on the
roadmap", and `guide/configuration.md`'s `virtual_chunk_containers` admonition
still says "Other storage backends such as `azure` are on the roadmap". Both are
stale: at this release Azure **is** a usable virtual-chunk location, for URLs
beginning `az://`, `azure://` or `abfs://`. **The code is the resolution.**
Exercising it needs a storage emulator, so none of it is graded — see
`TEST_INTERFACE.md` §14.

**R8-3 — `design/011` describes an object this release does not write.** The
design document specifies a flatbuffer object at `$ROOT/repo` holding the
repository's spec version, branches, tags, deleted tags and every
`SnapshotInfo`, and says that on open "spec version is checked, and an error
generated if trying to read from a newer repo." **This release implements only
the conservative half of that.** It never writes the `repo` object and cannot
parse one; it treats the object's *mere existence* as proof that the repository
was written by a newer generation, and refuses. Do not implement the rest of
`design/011` — not the object, not the deleted-tag list, not the ancestry index.
What is required is exactly `TEST_INTERFACE.md` §4.

**R8-4 — `guide/configuration.md` omits `NO_PROXY`.** Its proxy precedence list
names `HTTPS_PROXY`/`https_proxy`, then `HTTP_PROXY`/`http_proxy`, then
`ALL_PROXY`/`all_proxy`. The change log names `NO_PROXY` as a fourth honoured
variable. **All four are honoured.** Nothing about proxying is graded: it is
inherited from the AWS SDK's default HTTP client and there is no proxy-specific
identifier anywhere in the crate.

**R8-5 — `guide/performance.md` has a code typo.** Its concurrency example reads
`zarr.config.get({"async.concurrency": 128})`; `set` is plainly meant, since the
line before prints the value with `get`. Read it as `set`. Nothing follows from
it for the Rust crate.

**R8-6 — `guide/faq.md` overstates forward compatibility.** The previous
milestone's copy said "Data written by Icechunk 1.0 and greater will forever be
readable by future Icechunk versions." `guide/version-policy.md`, new here, is
narrower and is the resolution: a library of major version *N* reads and writes
spec versions *N* and *N−1*, older versions are best-effort, and **there is no
forward compatibility at all** — a 1.x library cannot read a format version 2
repository and must fail with a clear error rather than attempt it. `faq.md`'s
own text was reworded at this release in the same direction.

**R8-13 — the corpus carries two generations of the guide.** Each milestone's
corpus is overlaid on the previous one, and between milestones 05 and 06 upstream
moved its documentation from `icechunk-python/docs/` to `docs/`, renaming every
file. The thirteen `guide/icechunk-python-*.md` documents are therefore the
milestone 01–05 copies, left behind under names nothing overwrites. **They are
superseded in full.** Where such a file and its unprefixed successor disagree,
the unprefixed file wins; where only the prefixed file exists
(`icechunk-python-developing.md`), it describes a document the project has since
dropped and states nothing normative. `README.md` lists all thirteen.

## 3. The two forward-compatibility guards, and which one this milestone is about

The corpus does not distinguish them; the verifier does.

* **Per file, pre-existing.** Every snapshot, manifest and transaction-log
  object carries a spec-version byte at offset 36 of its 39-byte header, and a
  reader rejects any value but `1`. This is milestone 03's header, described in
  `design/004-binary-format.md`, and it is unchanged here. It fires when the
  *object* is parsed.
* **Per repository, new here.** An object at the key `repo` under the repository
  prefix means "a newer generation wrote this", and `Repository::open` refuses
  before doing anything else — before fetching the configuration, and before
  deciding whether a repository exists at all. The same check runs on commit,
  after the flush and before the branch reference moves, so the branch is never
  left pointing at a snapshot the newer generation did not authorise.

Both are graded. Their exact error kinds, and the byte layout of the header, are
in `TEST_INTERFACE.md` §§4.1–4.4.

## 4. The corpus is written for Python; the crate is Rust

Four of this release's headline statements do not translate directly.

**R8-7 — `Session.chunk_type` does not exist in Rust.** The change log's
"`Session.chunk_type` returns native / inline / virtual / uninitialized" names a
Python method, and `ChunkType` is a PyO3 enum. The Rust crate has no
`chunk_type`, no `ChunkType` and no `ChunkKind`. The equivalent — and what is
graded — is `Session::get_chunk_ref`, whose `Ok(None)` / `Ok(Some(Ref))` /
`Ok(Some(Inline))` / `Ok(Some(Virtual))` are exactly those four states. See
`TEST_INTERFACE.md` §5.

**R8-8 — `Repository.inspect_snapshot` does not exist in Rust.** The change log
names a Python method. The Rust surface is one free function,
`icechunk::inspect::snapshot_json(&AssetManager, &SnapshotId, bool)`, in a module
that is not re-exported at the crate root. See `TEST_INTERFACE.md` §11.

**R8-9 — `RepositoryConfig.merge` is not new.** The change log's "Added
`RepositoryConfig.merge` method" refers to a newly exposed Python binding. The
Rust method existed at the previous milestone with the same signature. What
actually changed in Rust is **one field's precedence rule** —
`virtual_chunk_containers` went from union to replace — **plus one new field**,
`max_concurrent_requests`. Those two are what is graded. See
`TEST_INTERFACE.md` §§3.1–3.3.

**R8-10 — anonymous credentials did not arrive "on every backend".** The change
log's anonymous-access entries are *fixes* to paths that already existed.
`S3Credentials::Anonymous` and `GcsCredentials::Anonymous` are unchanged since
milestone 07; **`AzureCredentials` has no anonymous variant at all** at this
release, and the Azure virtual-chunk fetcher carries an explicit to-do saying
so. Only S3 and GCS are graded. See `TEST_INTERFACE.md` §9.

## 5. Two more statements whose Rust reading is not the obvious one

**R8-11 — the raised size limits are private.** The change log says the maximum
allowed size of manifest and snapshot files was increased. In Rust those limits
are private statics inside the flatbuffers verifier options; nothing public
reports them, and reaching the old ceiling takes tens of millions of references.
The graded, public consequence is the matching memory accounting: the default
snapshot-node cache size rose from 30,000 to **500,000**, reachable through
`CachingConfig::num_snapshot_nodes()`. See `TEST_INTERFACE.md` §3.4.

**R8-12 — "a warning for a large number of chunks in a commit" is a log line.**
It is a `tracing::warn!` emitted once per change set from a private constant
threshold. Emit it; it is not graded, because grading it would make a log
message normative.

## 6. Resolutions carried forward from earlier milestones

These were settled before this milestone and still apply. They are restated here
because this corpus overlay replaces the earlier ones under the same file names,
and a resolution that is dropped is a resolution that is lost.

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
milestone's `TEST_INTERFACE.md` §4.4 — which is also where the byte-36 guard
this milestone still relies on is stated.

**R4-1 — the FlatBuffers file identifier is written but not verified.**
Metadata buffers carry `Ichk` at bytes 4..8. Icechunk writes it and discriminates
on the header's file-type byte instead, so a buffer whose identifier is absent
is still accepted. The benchmark checks that the identifier is present on
written buffers; it does not require it to be rejected on read.

**R5-1 — the command-line interface is undocumented in the corpus.** No guide
document describes the `icechunk` binary. Its grammar, exit statuses and
configuration file — including the fact that the configuration file's
repository definitions are written as YAML `!Variant` tags — are stated in
`TEST_INTERFACE.md` §12.4, which is the sole authority for them.

**R5-2 — chunk accounting used to see materialised chunks only.** At milestone
05 a chunk at or below `inline_chunk_threshold_bytes` contributed nothing to
`ops::stats::repo_chunks_storage`. **That is what changed at this milestone**:
the statistics now report inline and virtual bytes as well, in separate fields,
and the note that "the threshold must be lowered to measure chunk storage" no
longer applies to the totals — only to which of the three fields a chunk lands
in. Garbage collection still counts materialised chunk objects only, since an
inline chunk is not an object. See `TEST_INTERFACE.md` §6.

**R6-1 — "format 1.0" did not bump the version byte.** `SpecVersionBin::V0dot1`
is still `1`, and still is here. "Format frozen as 1.0" is the project's
compatibility policy — now written down properly in `guide/version-policy.md` —
not a wire-format change.

**R6-2 — the split-manifest design document is aspirational.**
`design/005-manifest-split.md` describes manifest sets, a maximum manifest size
and bin packing. None of that was implemented. The implemented model is the
chunk-space grid: one manifest per non-empty cell. See `TEST_INTERFACE.md`
§12.5.

**R6-3 — there are no default virtual chunk containers.** A repository
authorises virtual chunk access explicitly, by URL prefix, through the third
argument of `Repository::{create, open, open_or_create}`. `guide/virtual.md`'s
new security admonition is a restatement of exactly this.

**R7-1 — the asynchronous interface has no Rust counterpart.** Every I/O method
on `Repository`, `Session`, `Store`, `ops::gc` and `ops::stats` has been an
`async fn` since long before milestone 07. The `_async` methods `guide/async.md`
documents are PyO3 wrappers. Nothing in the Rust crate corresponds to them and
the verifier does not reach them.

**R7-2 — `validate_container` has no default in Rust.** It is a positional,
mandatory `bool` on `Store::set_virtual_ref` and `Store::set_virtual_refs`. The
default lives in the Python wrapper.

**R7-3 — the cache warnings are logs, not values.** A manifest or snapshot too
large for its cache produces a `tracing::warn!` from a private method. Emit it;
its text is not graded.

**R7-4 — `ManifestFileInfo` accessors are a Python fix.** The Rust struct's
fields have been `pub` throughout.

**R7-5 — the virtual-container separator rule.** `guide/configuration.md` writes
every container prefix with a trailing separator but never states that the
separator is **required**. It is, since milestone 07, and it remains so here:
`VirtualChunkContainer::new` rejects a prefix that does not end in `/`, and the
resolver repairs a persisted prefix or credential key that lacks one. The rule
is what makes authorisation prefix-exact rather than a raw substring test. See
`TEST_INTERFACE.md` §12.6.

## 7. Things the corpus says that are *not* requirements here

* The MinIO, AWS, Google Cloud Storage, Azure, Tigris and Cloudflare R2
  instructions in `guide/storage.md`, `guide/configuration.md` and
  `guide/virtual.md`. The verifier has no network and no object-storage
  service; only in-memory and local-filesystem storage are exercised, and a
  virtual chunk reference is recorded but never dereferenced.
* Everything in `guide/performance.md`'s `### Stalled Network Streams` and the
  change log's retry entries **as behaviour**. Retrying on HTTP 408, 429 or 499,
  on connection resets, or on a stalled stream requires a fault-injecting proxy
  in front of an object store. Only the configuration surface is graded.
* `## Proxy configuration` in `guide/configuration.md`, in full.
* The Dask, Xarray and Zarr guidance in `guide/dask.md`, `guide/xarray.md`,
  `guide/parallel.md`, `guide/concepts.md` and `guide/faq.md`. All of it is
  Python-level.
* The performance numbers anywhere in the corpus. This milestone is not a
  performance task and no throughput figure is graded.
