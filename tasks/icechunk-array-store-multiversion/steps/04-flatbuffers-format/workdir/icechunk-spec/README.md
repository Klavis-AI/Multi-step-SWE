# Icechunk specification corpus — milestone 04 (`v0.2.0`)

Verbatim copies of the public Icechunk specification, design documents,
FlatBuffers schemas and prose guides, pinned at the release this milestone
targets. **These files contain no implementation** — they are documentation and
schema text and nothing else. They are read-only inputs: never edit, trim,
annotate or extend them.

| | |
| --- | --- |
| Edition | `earth-mover/icechunk` tag `v0.2.0` |
| Source paths | `docs/docs/`, `design-docs/`, `icechunk/flatbuffers/` in that repository, plus `spec/icechunk-spec.md` from `icechunk-v0.1.0-alpha.1` under `archive/` |
| Licence | Apache-2.0 — see `LICENSE` and `NOTICE` in this directory |
| Integrity | `SHA256SUMS` covers the **34** verbatim files this milestone copied |

`README.md` and `PROFILE.md` — this file and the one beside it — are
**task-owned**, not upstream. They are deliberately absent from `SHA256SUMS`.

## What is here

The counts below are what you will actually find **in `/app/icechunk-spec/`**,
which is not the same as what this milestone's own overlay ships: the overlay
lays down 37 files — the 34 `SHA256SUMS` covers, plus `SHA256SUMS`, `README.md`
and `PROFILE.md` — and `/app` holds 38, because milestone 01's
`guide/icechunk-python-developing.md` is still there underneath. So `guide/`
holds 18 files on disk and 17 of them are this milestone's.

```text
/app/icechunk-spec/       38 files on disk; this milestone's overlay ships 37
  spec.md                 the normative Icechunk specification at this release
  flatbuffers/            5 files - the normative metadata IDL (NEW at this milestone)
  design/                 8 design documents
  guide/                  18 files - 17 prose guides copied at this milestone,
                          including a copy of spec.md, plus 1 left over below
  archive/spec-alpha1.md  the original long specification, historical only
  LICENSE  NOTICE  SHA256SUMS
  README.md  PROFILE.md   task-owned
```

**`/app` is cumulative, so this tree is too.** Each milestone's corpus is
*merged* into `/app/icechunk-spec/`, not swapped for it, and a document upstream
dropped or renamed between releases stays behind under its old name. At this
milestone exactly one such file is present:
`guide/icechunk-python-developing.md`, a contributor build guide carried over
from milestone 01 and gone from this release's documentation. It is **not
normative here** and it is deliberately absent from `SHA256SUMS`, which lists
only the 34 files this milestone copied — so `sha256sum -c SHA256SUMS` still
verifies 34 of 34. The rule for the whole tree: apart from `SHA256SUMS` itself
and the two task-owned files, a document it does not list is a leftover, and
`PROFILE.md` P13 says what to do with one.

## `spec.md` — the normative document

One document, 310 lines. `guide/spec.md` is the identical file under its
original documentation path; read either.

| Looking for | Section |
| --- | --- |
| What a repository is made of, and the read/write paths | "Overview" |
| The object-store key layout (`refs/`, `snapshots/`, `attributes/`, `manifests/`, `transactions/`, `chunks/`) | "File Layout" |
| The standing warning that metadata file formats are in flux | "File Formats" |
| Branches, tags, the `main` branch, the JSON body of a reference file | "Reference Files" |
| How a commit updates a branch, and the optimistic-concurrency argument | "Creating and Updating Branches" — **but see `PROFILE.md` D1** |
| The tag file name and tag immutability | "Tags" |
| That a snapshot file is FlatBuffers, and where its IDL lives | "Snapshot Files" |
| That the attributes file format is undefined and unimplemented | "Attributes Files" |
| That a manifest is FlatBuffers, and that it holds exactly three kinds of chunk reference — native (`Ref`), inline (`Inline`), virtual (`Virtual`) | "Chunk Manifest Files" |
| What may sit inside a chunk file | "Chunk Files" |
| Initialise, read from snapshot / branch / tag, write, create tag | "Algorithms" |
| The storage guarantees Icechunk needs from an object store | "Storage Operations" |

## `flatbuffers/` — the metadata IDL (new at this milestone)

These five files are the normative description of the snapshot, manifest and
transaction-log files. Read them before you read anything else in this
milestone.

| File | Declares |
| --- | --- |
| `all.fbs` | nothing of its own: it `include`s the other four, in order, and is the single unit `flatc` compiles. The generation command is in its header comment |
| `object_ids.fbs` | `struct ObjectId12` (12 bytes — snapshot, manifest, chunk, attributes ids) and `struct ObjectId8` (8 bytes — node ids) |
| `snapshot.fbs` | `MetadataItem`, `ManifestFileInfo`, `ChunkIndexRange`, `ManifestRef`, `DimensionShape`, `DimensionName`, `GroupNodeData`, `ArrayNodeData`, `union NodeData`, `NodeSnapshot`, `Snapshot`; `root_type Snapshot`. Its comments state the sorting invariants the readers depend on |
| `manifest.fbs` | `ChunkRef`, `ArrayManifest`, `Manifest`; `root_type Manifest`. Its leading comment explains why chunk references deliberately avoid a union |
| `transaction_log.fbs` | `ChunkIndices`, `ArrayUpdatedChunks`, `TransactionLog`; `root_type TransactionLog`. **See `PROFILE.md` D3**: two of its field comments are swapped |

## `design/` — the design documents

| File | What it decides |
| --- | --- |
| `001-virtual-chunk-improvements.md` | virtual chunk containers, URL prefixes, credentials, checksums |
| `002-rebase-commit-improvements.md` | conflict detection, the solver interface, rebase |
| `003-python-instantiation-api.md` | the Python construction API — background only |
| `004-binary-format.md` | **the 39-byte metadata-file header**, byte by byte. Its last row is stale — see `PROFILE.md` D2 |
| `005-manifest-split.md` | splitting manifests — a later milestone |
| `006-tag-delete.md` | tag deletion and the delete marker |
| `007-basic-expiration.md` | expiration ("update A in place") and garbage collection by reachability |
| `008-no-copy-serialization-formats.md` | **why FlatBuffers**: the defects of the previous format, the alternatives considered, the benchmark, and the decision |

## `guide/` — the prose guides

Written for the Python API. They are the best statement of *user-visible
semantics*; translate them to the Rust surface `TEST_INTERFACE.md` names.

| File | Useful for |
| --- | --- |
| `icechunk-python-version-control.md` | branches, tags, commits, ancestry, diff, conflict resolution |
| `icechunk-python-virtual.md` | virtual chunk references and containers |
| `icechunk-python-configuration.md` | `RepositoryConfig`, storage settings, caching, compression, inline threshold |
| `icechunk-python-concurrency.md`, `icechunk-python-parallel.md`, `icechunk-python-dask.md`, `icechunk-python-xarray.md` | distributed writes, session forking, the merge/commit protocol |
| `icechunk-python-storage.md` | the storage backends and their configuration |
| `icechunk-python-quickstart.md`, `icechunk-python-index.md`, `index.md`, `overview.md` | orientation |
| `icechunk-python-faq.md`, `faq.md` | logging, environment variables, assorted semantics |
| `icechunk-rust.md` | a one-paragraph pointer to the Rust crate's docs.rs page |
| `sample-datasets.md` | public example datasets — not usable offline |
| `spec.md` | byte-identical to the top-level `spec.md` |
| `icechunk-python-developing.md` | **not from this release** — a milestone-01 leftover, see above. Not normative |

## `archive/spec-alpha1.md`

The original, much longer specification as it stood at `icechunk-v0.1.0-alpha.1`
(with the embedded JSON Schema blocks). It is kept for history and is **not
normative at this milestone**: several of its statements were already untrue of
the code that shipped with it, and more of them are untrue now. `PROFILE.md`
lists the ones that matter.

## Where the corpus stops

The corpus does not contain the upstream release notes, the upstream test
suite, or the Rust API reference. Where a behaviour is neither in these
documents nor derivable from them, `PROFILE.md` and `/app/TEST_INTERFACE.md`
are the benchmark's answer, and `TEST_INTERFACE.md` governs.
