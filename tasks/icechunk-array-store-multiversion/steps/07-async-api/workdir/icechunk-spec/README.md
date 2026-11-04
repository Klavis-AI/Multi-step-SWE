# The Icechunk specification corpus — milestone 07

Fifty-five files, all of them documentation, all copied verbatim from
`earth-mover/icechunk`, except four the benchmark owns: this `README.md`,
`PROFILE.md`, `NOTICE` and `SHA256SUMS` — the same four `NOTICE` names.
`NOTICE` records the provenance and the licence; `SHA256SUMS` has thirty-nine
entries, the thirty-eight files this milestone itself copied plus the
task-written `NOTICE`.

This is a reference corpus. It contains no implementation, and nothing in it is
generated from the code you are writing. Do not edit it.

```
icechunk-spec/
├── README.md          this file
├── PROFILE.md         the benchmark's resolutions; read it second
├── NOTICE, LICENSE, SHA256SUMS
├── spec.md            the normative format specification at this release
├── archive/           1 file  — the original spec-1 document
├── design/            9 files — the project's design documents
├── flatbuffers/       5 files — the normative binary schemas
└── guide/            34 files — 21 current, plus 13 superseded; see below
```

**`guide/` holds two generations of the same documentation.** Twenty-one files
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

They are out of date and **nothing in them is normative here** — where such a
file and its unprefixed successor disagree, read the unprefixed one.
(`icechunk-python-developing.md` has no successor at all; it was a contributor
guide and was dropped.) They are not listed in `SHA256SUMS`, which covers only
what this milestone itself shipped. See `PROFILE.md` §5.

## Where to read first

For **this** milestone, in order:

1. `PROFILE.md` — what the corpus leaves open and how this benchmark resolves
   it, plus which documents carry this release's new statements.
2. `guide/async.md` — **new at this release**, 19 lines. The asynchronous
   interface: that it exists, that it is optional, that the core is already
   fully asynchronous either way, when to use it, and the `_async` postfix
   naming rule with a worked `Repository.open_async` / `list_branches_async`
   example.
3. `guide/configuration.md` — changed at this release. The
   `virtual_chunk_containers` section is the one that matters. Its container
   examples were already separator-terminated (`"s3://my-s3-bucket/"`,
   `"s3://my-other-s3-bucket/"`) one milestone ago; what changed here is the
   constructor's shape — a positional prefix plus `store=` — and one new
   sentence saying that a second `set_virtual_chunk_container` call with a
   different prefix **adds** a container rather than overwriting the first, so a
   virtual chunk url is served by whichever container's prefix it starts with.
   It does **not** say what happens when two prefixes both match; that
   tie-break — the longest prefix wins — is stated only in
   `/app/TEST_INTERFACE.md` §1.2. Its "Virtual Chunk Credentials" section
   explains that containers not named in `authorize_virtual_chunk_access`
   cannot fetch, and keys its own example map with an *unseparated* prefix
   (`"s3://my_s3_bucket"`) — the persisted shape §1.2 requires this release to
   normalise rather than reject.
4. `guide/howto.md` — changed; new section *Usage in async contexts*, showing
   `await repo.garbage_collect_async(expiry_time)` and linking to `async.md`.
5. `guide/faq.md` — changed; new section *Does Icechunk work with Zarr
   sharding?*. Also carries *Does `icechunk-python` include logging?*, which is
   the context for this release's cache warnings.
6. `guide/virtual.md` — changed at this release by a single anchor fix. It is
   still the place that explains virtual chunk references end to end.

## The rest of the corpus

`spec.md` is the normative on-disk format specification. It is **unchanged at
this release** — no format-version bump, no schema change — and
`guide/spec.md` is the same document under its documentation-site path.
`flatbuffers/{all,snapshot,manifest,transaction_log,object_ids}.fbs` are the
normative binary schemas it refers to, also unchanged.

`guide/performance.md` is **unchanged at this release**; it remains the
reference for manifest preloading, splitting and caching, but introduces no new
requirement here.

The other guide documents cover the surface earlier milestones built and this
one must preserve: `overview.md` and `quickstart.md` (orientation),
`version-control.md` (branches, tags, snapshots, conflicts, rebase),
`expiration.md` (expiration and garbage collection), `storage.md` (backends and
their settings), `concurrency.md`, `parallel.md` and `dask.md` (distributed
writes), `xarray.md` and `sample-datasets.md`, `migration.md`,
`icechunk-for-git-users.md`, `icechunk-rust.md` (the Rust crate's own short
page) and `index.md`.

`design/` holds the project's design documents. They are motivation, not
behaviour — `design/005-manifest-split.md` in particular describes a design that
was never implemented as written. `design/004-binary-format.md` is the closest
thing to a written account of the file header. `archive/spec-alpha1.md` is the
original long specification, kept for historical reference; where it and
`spec.md` disagree, `spec.md` wins.

## What the corpus does not tell you

The corpus is the project's user documentation, not its Rust API reference. It
documents the Python package, and several of this release's Rust-observable
changes — the separator rule's exact error, the widened conflict-solver bound,
the opaque `VirtualChunkLocation`, the new garbage-collection parameters, the
renamed ancestry stream — appear in it only indirectly or not at all.
`/app/TEST_INTERFACE.md` is the authority for those, and `PROFILE.md` records
where the two documents meet.
