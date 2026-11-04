# Icechunk specification corpus — milestone 05

Verbatim copies of `earth-mover/icechunk`'s own specification, guides, design
documents and binary schemas, pinned at the release this milestone is anchored
to. **These files contain no implementation** — they are documentation and
nothing else. They are read-only inputs: never edit, trim, annotate or extend
them.

| | |
| --- | --- |
| Upstream | <https://github.com/earth-mover/icechunk> |
| Licence | Apache-2.0 — see `LICENSE` and `NOTICE` beside this file |
| Integrity | `SHA256SUMS` lists the **36** upstream documents this milestone ships (itself excluded) |
| Task-owned | only `README.md` (this file) and `PROFILE.md` |

This milestone ships **37 files**: `spec.md`, `LICENSE`, `NOTICE`,
`SHA256SUMS`, one archived specification under `archive/`, nine design
documents under `design/`, five FlatBuffers schemas under `flatbuffers/`, and
eighteen guide documents under `guide/`.

Each milestone's documents are **merged** into `/app` rather than swapped for
the previous milestone's, so a file an earlier milestone shipped under a name
this one does not reuse is still on disk. Exactly one such file survives here:
`guide/icechunk-python-developing.md`, which milestone 01's corpus carried and
upstream has since removed. It is **not normative at this milestone**, it is not
listed in `SHA256SUMS`, and no scored group reads it. So the tree you actually
see holds **38** upstream files and **19** documents under `guide/`, one more
than the count above; every other file is replaced name for name by this
milestone's copy.

## What is new in this milestone's corpus

Compared with the previous milestone's corpus, **two files are new** and nine
changed:

- **new**: `guide/icechunk-python-expiration.md`, `design/009-tigris-support.md`
- **changed**: `guide/icechunk-python-{dask,faq,index,parallel,quickstart,storage,version-control,xarray}.md`
  and `guide/sample-datasets.md`

`spec.md` and `guide/icechunk-python-configuration.md` are **byte-identical** to
the previous milestone's copies — the on-disk format and the configuration
surface they describe did not change. `PROFILE.md` records what that means for
this milestone.

## The documents

| File | What it covers |
| --- | --- |
| `spec.md` | **The Icechunk specification.** Goals, the storage operations an object store must provide, the file layout, reference files, snapshot files, attributes files, chunk manifest files, chunk files, and the algorithms for initialising a repository, reading from one, writing a snapshot and creating a tag. `guide/spec.md` is the same document at its documentation path. |
| `archive/spec-alpha1.md` | The original specification from the project's first public release. Background for how the format arrived where it is. |
| `flatbuffers/*.fbs` | The normative binary schemas: `all.fbs`, `object_ids.fbs`, `snapshot.fbs`, `manifest.fbs`, `transaction_log.fbs`. |
| `guide/overview.md`, `guide/index.md`, `guide/faq.md` | What Icechunk is, what problem it solves, and the project-level FAQ. |
| `guide/icechunk-rust.md` | The Rust library's own short guide. |
| `guide/icechunk-python-quickstart.md` | End-to-end narrative: create a repository, open a session, write, commit. |
| `guide/icechunk-python-version-control.md` | Branches, tags, snapshots, history, time travel. |
| `guide/icechunk-python-configuration.md` | Repository configuration field by field: inlining threshold, partial-value concurrency, compression, caching, storage settings, virtual chunk containers, manifest preload, and how configuration is applied and persisted. Its `storage` section (line 54) is **out of date** — see `PROFILE.md` P16. |
| `guide/icechunk-python-storage.md` | **Per-backend configuration**: S3, Tigris, Cloudflare R2, Minio, Google Cloud Storage, Azure Blob, filesystem and in-memory, with each backend's limitations. |
| `guide/icechunk-python-expiration.md` | **Expiring data**: what `expire_snapshots` releases, what `garbage_collect` deletes, and why inlining must be disabled for chunks to become collectable objects. |
| `guide/icechunk-python-parallel.md` | **Parallel and distributed writes**: multi-threading, cooperative writes that merge sessions, and uncooperative writes that retry on conflict. |
| `guide/icechunk-python-dask.md` | Distributed writes driven by Dask, including the session reduction. |
| `guide/icechunk-python-concurrency.md` | The concurrency model and its guarantees. |
| `guide/icechunk-python-virtual.md` | Virtual chunk references and virtual chunk containers. |
| `guide/icechunk-python-xarray.md` | Writing Xarray datasets. |
| `guide/icechunk-python-index.md`, `guide/icechunk-python-faq.md` | The Python library's index and FAQ. |
| `guide/sample-datasets.md` | Public datasets published as Icechunk repositories. |
| `design/001-virtual-chunk-improvements.md` | Improvements to virtual chunk handling. |
| `design/002-rebase-commit-improvements.md` | Conflict detection and rebasing. |
| `design/003-python-instantiation-api.md` | Repository instantiation and configuration API. |
| `design/004-binary-format.md` | The on-disk binary format and its file headers. Its payload row is stale — see `PROFILE.md` D2. |
| `design/005-manifest-split.md` | Multiple manifests per snapshot. |
| `design/006-tag-delete.md` | Deleting tags. |
| `design/007-basic-expiration.md` | **The expiration algorithm**: what is released, what is edited, and why the first snapshot is never expired. |
| `design/008-no-copy-serialization-formats.md` | Why FlatBuffers. |
| `design/009-tigris-support.md` | **Tigris support**: why the backend needs special handling, the region header, and what the weak-consistency mode gives up. |

## Where to look for what

| Looking for | Document and section |
| --- | --- |
| The repository's file layout on an object store | `spec.md` → `### File Layout`, `### File Formats` |
| How a branch or tag reference is stored and updated | `spec.md` → `### Reference Files`. Its `#### Creating and Updating Branches` sub-section is **stale** — see `PROFILE.md` D1 |
| What a snapshot, manifest or chunk file contains | `spec.md` → `### Snapshot Files`, `### Chunk Manifest Files`, `### Chunk Files` |
| The steps of initialising, reading and committing | `spec.md` → `## Algorithms` |
| The exact binary encoding of those files | `flatbuffers/*.fbs`, `design/004-binary-format.md` — both carry stale text, see `PROFILE.md` D2–D4 |
| What expiration releases and what collection deletes | `guide/icechunk-python-expiration.md`; `design/007-basic-expiration.md` |
| Why the first snapshot is never *released* | `design/007-basic-expiration.md`; `guide/icechunk-python-expiration.md` → `## Expire snapshots`. Both say "never expired"; they mean it is never dropped from the history. What `ExpireRefResult::ref_is_expired` reports about a tip that *is* the first snapshot is settled by `/app/TEST_INTERFACE.md` section 4.10.2, which is normative here. |
| Why inlining must be off for chunks to be collectable | `guide/icechunk-python-expiration.md` → `# Expiring Data` |
| Ordering of `ancestry`, and using a commit time as a cut-off | `guide/icechunk-python-expiration.md` → `## Expire snapshots` |
| `force_path_style`, and which stores need it | `guide/icechunk-python-storage.md` → `#### Minio` |
| Cloudflare R2: account id, endpoint derivation, public buckets | `guide/icechunk-python-storage.md` → `#### Cloudflare R2` |
| Tigris: why it has its own constructor, and its region | `guide/icechunk-python-storage.md` → `#### Tigris`; `design/009-tigris-support.md` → `## How to achieve it` |
| Why weak-consistency Tigris is read-only | `design/009-tigris-support.md` → `## How to achieve it` |
| Filesystem and in-memory backend limitations | `guide/icechunk-python-storage.md` → `### Filesystem Storage`, `### In Memory Storage` |
| Every field of the repository configuration | `guide/icechunk-python-configuration.md` |
| What compression applies to | `guide/icechunk-python-configuration.md` → `### compression` ("its **metadata** files"). Chunk objects are not compressed by the repository, so `ops::stats::repo_chunks_storage` over *n* chunks of *b* bytes is `n · b` — `/app/TEST_INTERFACE.md` section 4.10 states it. |
| Conditional writes and the `unsafe_use_*` switches | **not in this corpus** — `PROFILE.md` P16 and `/app/TEST_INTERFACE.md` section 4.8 |
| Cooperative distributed writes and session merging | `guide/icechunk-python-parallel.md` → `## Distributed writes`, `### Cooperative distributed writes` |
| The Dask reduction over sessions | `guide/icechunk-python-dask.md` |
| Optimistic concurrency, conflicts and rebase | `guide/icechunk-python-concurrency.md`; `design/002-rebase-commit-improvements.md` |
| Virtual chunk references and containers | `guide/icechunk-python-virtual.md`; `design/001-virtual-chunk-improvements.md` |
| Branches, tags and time travel | `guide/icechunk-python-version-control.md` |

**The command line is not in this corpus.** No document here mentions the
`icechunk` executable at all; the grammar, exit statuses, output shape and
configuration file this milestone requires are fixed by
`/app/TEST_INTERFACE.md` section 5, which is normative for them. `PROFILE.md`
beside this file records that and the benchmark's other resolutions where these
documents leave behaviour open, are silent, or have fallen out of date.
