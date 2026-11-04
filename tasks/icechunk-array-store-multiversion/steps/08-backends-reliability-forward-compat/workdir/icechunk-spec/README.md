# The Icechunk specification corpus — milestone 08

Fifty-nine files, all of them documentation, all copied verbatim from
`earth-mover/icechunk`, except this `README.md` and `PROFILE.md`, which the
benchmark owns. `NOTICE` records the provenance and the licence; `SHA256SUMS`
checksums the forty-three files this milestone itself copied.

This is a reference corpus. It contains no implementation, and nothing in it is
generated from the code you are writing. Do not edit it.

```
icechunk-spec/
├── README.md          this file
├── PROFILE.md         the benchmark's resolutions; read it second
├── NOTICE, LICENSE, SHA256SUMS
├── spec.md            the normative format specification at this release
├── archive/           1 file  — the original spec-1 document
├── design/           11 files — the project's design documents (2 new here)
├── flatbuffers/       5 files — the normative binary schemas
└── guide/            36 files — 23 current, plus 13 superseded; see below
```

**`guide/` holds two generations of the same documentation.** Twenty-three files
are this release's, under their current names. Thirteen more are named
`icechunk-python-*.md` and are **left over from milestones 01 to 05**, when the
project published its documentation from a different directory; each has since
been renamed, and its current successor is the file with the same name minus the
`icechunk-python-` prefix:

```
icechunk-python-concurrency.md      icechunk-python-parallel.md
icechunk-python-configuration.md    icechunk-python-quickstart.md
icechunk-python-dask.md             icechunk-python-storage.md
icechunk-python-developing.md       icechunk-python-version-control.md
icechunk-python-expiration.md       icechunk-python-virtual.md
icechunk-python-faq.md              icechunk-python-xarray.md
icechunk-python-index.md
```

They are years out of date and **nothing in them is normative here** — read the
unprefixed file instead. (`icechunk-python-developing.md` has no successor at
all; it was a contributor guide and was dropped.) They are not listed in
`SHA256SUMS`, which covers only the forty-three files this milestone copied.
Provenance: they are earlier milestones' verbatim copies, taken from the same
`earth-mover/icechunk` repository under the same Apache-2.0 licence at *their*
upstream tags rather than at this milestone's, which is the one place `NOTICE`'s
"at the upstream tag this milestone is anchored to" has to be read loosely.

## Where to read first

For **this** milestone, in order:

1. `PROFILE.md` — what the corpus leaves open, the six places where it
   contradicts itself or the code at this release, the two generations of
   documentation it carries, and how the benchmark resolves each of them.
2. `guide/version-policy.md` — **new at this release**, and the document behind
   this milestone's headline capability. It states that the on-disk format is
   versioned by a single integer, that a library's major version matches the
   format version it writes, and — under *Compatibility* — that Icechunk
   libraries do **not** read or write forward: a 1.x library cannot read format
   version 2, and must say so rather than misparse.
3. `guide/configuration.md` — changed. The new section
   `### max_concurrent_requests` documents the per-repository cap on concurrent
   object-store requests. The new section `## Proxy configuration` documents the
   proxy environment variables and their precedence. The
   `virtual_chunk_containers` section is otherwise as it was at the previous
   milestone, including its separator-terminated prefixes.
4. `guide/performance.md` — changed. The whole `## Concurrency` block is new:
   Zarr's per-operation `async.concurrency`, how Dask multiplies it, Icechunk's
   own global limit with a default of 256, and `### Stalled Network Streams`,
   which describes stall detection and the `network_stream_timeout_seconds`
   knob. **Its stated default is wrong; see `PROFILE.md` §R8-1.**
5. `design/011-ref-and-ancestry-entry-point.md` — **new at this release**. It
   describes a single `$ROOT/repo` object holding a repository's spec version,
   references and snapshot list. That design is **not implemented here**, but it
   is the reason the key `repo` is reserved and why this release treats an
   object at that key as "written by a newer generation". See `PROFILE.md`
   §R8-3.
6. `design/010-notes-towards-an-IC-2.md` — **new at this release**. Motivation
   for the above, including the observation that there is currently no direct
   way to learn a repository's spec version.
7. `guide/concepts.md` — **new at this release**. A consolidated statement of
   the repository / session / snapshot / branch / tag model, transactions and
   ACID isolation. It restates milestones 01–03 rather than adding a
   requirement, and is the best single orientation document in the corpus.
8. `guide/virtual.md` — changed by one addition: a security admonition stating
   that Icechunk refuses to read an external virtual-chunk location unless the
   writer declared a trusted container and the reader passed
   `authorize_virtual_chunk_access`. That is milestone 06's rule, restated.

## The rest of the corpus

`spec.md` is the normative on-disk format specification, and it is **unchanged
at this release** — no format-version bump, no schema change. `guide/spec.md` is
the same document under its documentation-site path.
`flatbuffers/{all,snapshot,manifest,transaction_log,object_ids}.fbs` are the
normative binary schemas it refers to, also unchanged. A repository written by
the previous milestone must therefore open and read identically here.

`guide/storage.md` is **unchanged at this release**, including its
`### Azure Blob Storage` section — so it is not evidence that Azure is a new
backend here. `guide/faq.md`, `guide/overview.md` and
`guide/icechunk-for-git-users.md` changed only in prose that restates existing
behaviour.

The other guide documents cover the surface earlier milestones built and this
one must preserve: `quickstart.md` (orientation), `version-control.md`
(branches, tags, snapshots, conflicts, rebase), `expiration.md` (expiration and
garbage collection), `async.md` (the asynchronous interface),
`concurrency.md`, `parallel.md` and `dask.md` (distributed writes),
`howto.md`, `xarray.md`, `sample-datasets.md`, `migration.md`,
`icechunk-rust.md` (the Rust crate's own short page) and `index.md`.

`design/` holds the project's design documents. They are motivation, not
behaviour — `design/005-manifest-split.md` describes a design that was never
implemented as written, and `design/010`/`011` describe one that is not
implemented at this release either. `design/004-binary-format.md` is the closest
thing to a written account of the 39-byte object header.
`archive/spec-alpha1.md` is the original long specification, kept for historical
reference; where it and `spec.md` disagree, `spec.md` wins.

## What the corpus does not tell you

The corpus is the project's user documentation, not its Rust API reference. It
documents the Python package, and several of this release's Rust-observable
changes appear in it only indirectly or not at all: the precedence rules of
`RepositoryConfig::merge`, the storage-statistics split across native, virtual
and inline chunks, the four chunk-reference states, the delete-then-recreate
rebase, `Session::flush`, the conditional `reset_branch`, and the exact error
kind a newer-generation repository produces. `/app/TEST_INTERFACE.md` is the
authority for all of those, and `PROFILE.md` records where the two documents
meet.
