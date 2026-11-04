# The Icechunk specification corpus — milestone 06

Everything under this directory is **upstream documentation copied verbatim**
from `earth-mover/icechunk` at tag `v1.0.0` (Apache-2.0; see `LICENSE` and
`NOTICE`). Nothing here is implementation: there is no Rust, no Python and no
build system in this tree. Three files are written by this task rather than
copied — this `README.md`, `PROFILE.md` and `NOTICE` — and each says so at the
top.

`SHA256SUMS` has 38 entries: the 37 verbatim upstream files this milestone
copied (`LICENSE`, `spec.md`, 20 under `guide/`, 9 under `design/`, 5 under
`flatbuffers/` and `archive/spec-alpha1.md`) plus the task-written `NOTICE`. It
deliberately does not list itself, `README.md` or `PROFILE.md`, and it does not
cover the thirteen superseded files described below, which earlier milestones
copied.

## What is here

```
spec.md                        the normative specification at this release
guide/                         33 files — 20 current, plus 13 superseded
design/                        9 design documents
flatbuffers/                   5 normative binary schemas
archive/spec-alpha1.md         the original long spec-1 document, for history
```

`guide/spec.md` is a byte-identical copy of `spec.md`; it is there because the
guide is copied wholesale. Read either.

**`guide/` carries two generations of the same documentation.** Each
milestone's corpus is copied *on top of* the previous one, and between
milestones 05 and 06 upstream moved its documentation out of
`icechunk-python/docs/` and renamed every file. Twenty files are this release's,
under their current names; thirteen more are named `icechunk-python-*.md` and
are left over from milestones 01–05, under names nothing here overwrites:

```
icechunk-python-concurrency.md      icechunk-python-quickstart.md
icechunk-python-configuration.md    icechunk-python-storage.md
icechunk-python-dask.md             icechunk-python-version-control.md
icechunk-python-developing.md       icechunk-python-virtual.md
icechunk-python-expiration.md       icechunk-python-xarray.md
icechunk-python-faq.md
icechunk-python-index.md
icechunk-python-parallel.md
```

**Nothing in them is normative here.** Each one's successor is the file with the
same name minus the `icechunk-python-` prefix — read that instead. The one
exception is `icechunk-python-developing.md`, a contributor guide the project
dropped, which has no successor and states nothing this milestone requires. See
`PROFILE.md` P6-10.

## Where to start, for this milestone

Read in this order. The first three are the ones this milestone turns on.

| Read | Why |
| --- | --- |
| `PROFILE.md` | **Read this first.** It records every place where these documents and the shipped behaviour disagree, and how this task resolves the disagreement. Where `PROFILE.md` and a document conflict, `PROFILE.md` wins. |
| `guide/performance.md` | Manifest splitting and manifest preloading: the split-condition grammar, the per-dimension grammar, splitting behaviour on commit, `rewrite_manifests`, and the default preload configuration. This is the headline capability of the milestone. |
| `guide/migration.md` | The breaking changes at this release: `authorize_virtual_chunk_access`, and the removal of the built-in virtual chunk containers. |
| `guide/configuration.md` | `RepositoryConfig` field by field, including `manifest`, `storage` and `virtual_chunk_containers`, and how a configuration is applied and persisted. |
| `spec.md` | The on-disk format: storage operations, file layout, reference files, snapshot files, chunk manifest files, transaction logs, and the algorithms for initialising a repository and writing a snapshot. §"Initialize New Repository" is where the well-known first-snapshot name comes from. |
| `guide/virtual.md` | Virtual chunk references and the backends that may serve them. |
| `guide/version-control.md` | Branches, tags, time travel, conflict detection and conflict resolution. |
| `guide/expiration.md` | Snapshot expiration and garbage collection. |
| `flatbuffers/*.fbs` | The normative schemas behind every metadata object. `all.fbs` includes the other four. |
| `guide/storage.md`, `guide/faq.md` | Backends and their limitations; the compatibility policy and the logging control. |

Documents that are background rather than requirements for this milestone:
`guide/{index,overview,quickstart,howto,icechunk-for-git-users,sample-datasets,
xarray,dask,parallel,concurrency,icechunk-rust}.md`, `archive/spec-alpha1.md`,
and every `guide/icechunk-python-*.md`.

## Two standing warnings about this corpus

1. **The prose documents are written against the Python package.** The tag
   `v1.0.0` is the Python package version; the Rust crate at that tag is
   `icechunk 0.3.0`. Python spellings (`ManifestSplitCondition.and_conditions`,
   `Session.commit(rebase_with=...)`, `StorageRetriesSettings`,
   `set_logs_filter`) frequently have a *different* Rust spelling, or no Rust
   spelling at all. `PROFILE.md` §"Python spellings with no Rust equivalent"
   lists every one this milestone touches, and `/app/TEST_INTERFACE.md` gives
   the Rust signature that is actually required.
2. **`design/005-manifest-split.md` describes a design that was never
   implemented.** It is byte-identical to the copy that shipped one release
   earlier, and none of its identifiers exist in the released code. See
   `PROFILE.md` P6-6. Use `guide/performance.md` instead.
