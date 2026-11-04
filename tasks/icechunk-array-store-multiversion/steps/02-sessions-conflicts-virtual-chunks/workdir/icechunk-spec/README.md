# Icechunk specification corpus — milestone 02

Verbatim copies of the Icechunk project's own documents, pinned at the upstream
tag this milestone is anchored to (`icechunk-v0.1.0-alpha.8`), plus the previous
milestone's specification kept under `archive/`. **These files contain no
implementation.** They are read-only inputs: never edit, trim, annotate or
extend them.

| Edition | `earth-mover/icechunk` tag `icechunk-v0.1.0-alpha.8`, crate version `0.1.0-alpha.8` |
| --- | --- |
| Previous edition | `icechunk-v0.1.0-alpha.1`, kept as `archive/spec-alpha1.md` |
| Licence | Apache-2.0 — see `LICENSE` and `NOTICE` in this directory |
| Integrity | `SHA256SUMS` in this directory covers the 21 upstream files this milestone ships — every file it ships except `README.md`, `PROFILE.md` and `SHA256SUMS` itself |

`README.md` (this file) and `PROFILE.md` are **task-owned**, not upstream, and
are therefore not listed in `SHA256SUMS`.

**One file on disk is left over from milestone 01.** `/app` is cumulative and a
milestone's documents are *merged* into it rather than replacing the directory,
so `guide/` holds **15** files here: the 14 this milestone ships, listed below,
plus `guide/icechunk-python-developing.md`, which milestone 01 shipped and
upstream deleted before this release. It is stale, it is not covered by
`SHA256SUMS`, and nothing at this milestone is scored against it — see
`PROFILE.md` §R2. Every other file in this directory was replaced by this
milestone's edition.

## The documents on disk

```text
icechunk-spec/
  spec.md                       the normative specification at this tag
  archive/spec-alpha1.md        the specification as milestone 01 shipped it
  design/001-virtual-chunk-improvements.md
  design/002-rebase-commit-improvements.md
  design/003-python-instantiation-api.md
  guide/…                       the project's prose documentation at this tag
                                (14 files, plus one left over from milestone 01)
  LICENSE  NOTICE  SHA256SUMS   provenance
  README.md  PROFILE.md         task-owned: this index, and the resolutions
```

| File | What it carries |
| --- | --- |
| `spec.md` | **The normative specification.** Storage operations, the repository overview, file layout, reference files (branches, tags, the sequence-number encoding), snapshot files, attributes files, chunk manifest files, chunk files, and the algorithms for initialising, reading, writing and tagging. 348 lines. |
| `archive/spec-alpha1.md` | The specification document milestone 01 was built against, kept for comparison. It is much longer (1 094 lines) because it embedded JSON Schema blocks that `spec.md` has since replaced with the Rust data structures. Where the two disagree, **`spec.md` is the normative one for this milestone**; `PROFILE.md` §R1 lists what changed. |
| `design/001-virtual-chunk-improvements.md` | **The source of record for virtual chunk containers.** Container name / URL prefix / object-store configuration, the longest-prefix-first matching algorithm, the default container set, optional checksums (ETag and last-modified), per-container credentials, and the global `RepositoryConfig` stored as YAML at `repo-root/config.yaml` with a conditional write. |
| `design/002-rebase-commit-improvements.md` | **The source of record for commit conflicts and rebase.** `commit` may fail with a conflict carrying both parents; `rebase(solver)` then `commit`; the failure report naming the snapshot and the list of conflict reasons; inspecting a conflict by path and reason; `BasicConflictSolver`. |
| `design/003-python-instantiation-api.md` | Repository instantiation and configuration: `Repository.create` / `open` / `open_or_create`, `save_config` as an explicit destructive commit-less operation, and refreshable credentials. |
| `guide/spec.md` | A byte-identical copy of `spec.md`, because upstream publishes the specification inside its documentation site as well. Read `spec.md`. |
| `guide/overview.md` | Groups, arrays and chunks; snapshots; branches and tags; the three chunk reference kinds. The best short orientation. |
| `guide/icechunk-python-quickstart.md` | The end-to-end story of this milestone in Python: create a repository, take a writable session on `main`, commit, take a read-only session at an earlier snapshot. |
| `guide/icechunk-python-configuration.md` | Storage configuration, `inline_chunk_threshold_bytes`, and creating vs opening a repository. Its "Virtual Reference Storage Config" section is **stale** — see `PROFILE.md` §D1. |
| `guide/icechunk-python-virtual.md` | Writing virtual datasets. Its "Virtual Reference Storage Support" section is **stale** — see `PROFILE.md` §D1. |
| `guide/icechunk-python-concurrency.md` | **A stub at this tag**: headings and a TODO, no prose. See `PROFILE.md` §D2. |
| `guide/icechunk-python-version-control.md` | **A stub at this tag**: the words "COMING SOON!". See `PROFILE.md` §D2. |
| `guide/icechunk-python-dask.md` | Distributed writes: which Dask schedulers are supported and whose responsibility disjointness is. |
| `guide/icechunk-python-xarray.md` | The Xarray integration. Background only. |
| `guide/icechunk-rust.md` | One page saying the Rust library is used internally by the Python one. Background only. |
| `guide/faq.md`, `guide/index.md`, `guide/icechunk-python-index.md`, `guide/sample-datasets.md` | Background and navigation. `sample-datasets.md` names public S3 buckets; nothing in this milestone reaches the network. |
| `guide/icechunk-python-developing.md` | **Not part of this milestone's corpus.** Milestone 01's copy, left on disk by the cumulative overlay; upstream removed the document before this release. Stale, unchecksummed, never scored. |

## Where to look for this milestone's requirements

| Looking for | Read |
| --- | --- |
| The `Repository` / `Session` / `Store` split | `guide/icechunk-python-quickstart.md`, then `design/003-python-instantiation-api.md` |
| When a commit fails, and what the failure carries | `design/002-rebase-commit-improvements.md` → "Proposal" |
| What `rebase` does over several intervening commits | `design/002-rebase-commit-improvements.md` → "Proposal" |
| The conflict classes themselves | **Not in any prose document at this tag** — `PROFILE.md` §D2 enumerates them |
| Persisted repository configuration and overriding it | `design/001-virtual-chunk-improvements.md` → "Global config"; `design/003-python-instantiation-api.md` → "Discussion" |
| `inline_chunk_threshold_bytes` and its default | `guide/icechunk-python-configuration.md` → "Writing chunks inline" |
| Virtual chunk containers, and how one is chosen | `design/001-virtual-chunk-improvements.md` → "Virtual chunk containers", "Reading virtual chunks", "Default `VirtualChunkContainers`" |
| Virtual chunk checksums | `design/001-virtual-chunk-improvements.md` → "Checksum" |
| Per-container credentials | `design/001-virtual-chunk-improvements.md` → "Credentials" (both sections) |
| Branches, tags and the reference file layout | `spec.md` → "Reference Files", "Creating and Updating Branches", "Tags" |
| Snapshots, manifests and the three chunk reference kinds | `spec.md` → "Snapshot Files", "Chunk Manifest Files", "Chunk Files" |
| The read and write algorithms | `spec.md` → "Algorithms" |
| Distributed writes | `guide/icechunk-python-dask.md` → "Icechunk + Dask" |

`PROFILE.md` beside this file records the benchmark's resolutions where these
documents leave behaviour open, contradict each other, or have fallen behind
the release they ship with.
