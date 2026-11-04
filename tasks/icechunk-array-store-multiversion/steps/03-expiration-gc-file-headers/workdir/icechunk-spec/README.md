# Icechunk specification corpus

Verbatim copies of the `earth-mover/icechunk` documents published at the release
this milestone is anchored to. **These files contain no implementation** — they
are documentation and nothing else, and they are read-only inputs: never edit,
trim, annotate or extend them.

| Release | `earth-mover/icechunk` tag `icechunk-v0.1.0` (crate `icechunk` 0.1.0) |
| --- | --- |
| Previous milestone | `icechunk-v0.1.0-alpha.8` |
| Source paths | `docs/docs/`, `design-docs/` in that repository |
| Licence | Apache-2.0 — see `LICENSE` and `NOTICE` in this directory |
| Integrity | `SHA256SUMS` in this directory covers the 27 files this milestone copied |

`README.md`, `PROFILE.md` and `SHA256SUMS` are task-owned; everything else,
`LICENSE` and `NOTICE` included, is upstream text or its provenance.

**What you will actually find in `/app/icechunk-spec/` is 31 files**, because
each milestone's corpus is *overlaid* on the previous one: an overlay writes
same-named files but deletes none. Thirty of the 31 are this milestone's — the
27 upstream files `SHA256SUMS` covers, plus the three task-owned ones. The
thirty-first is `guide/icechunk-python-developing.md`, shipped by milestone 01
and dropped by upstream afterwards; it is superseded, non-normative, and not in
`SHA256SUMS`. See `PROFILE.md` §D11.

```
icechunk-spec/                            31 files on disk
├── README.md        task-owned           this file
├── PROFILE.md       task-owned           read it second
├── SHA256SUMS       task-owned           checksums the 27 files below
├── LICENSE          checksummed          Apache-2.0
├── NOTICE           checksummed          provenance
├── spec.md          checksummed          the normative specification
├── archive/         checksummed   1      the original spec-1 document
├── design/          checksummed   7      001–003 from milestone 2,
│                                         004–007 new at this release
└── guide/           checksummed  16      this release's prose guide
    └── icechunk-python-developing.md  1  superseded, NOT checksummed
```

So `guide/` holds 17 files and the directory holds 31, while `SHA256SUMS`
lists 27 and this milestone shipped 30.

## Start here for this milestone

| Looking for | Read |
| --- | --- |
| The 39-byte header on snapshot, manifest and transaction-log files | `design/004-binary-format.md` — the **only** place it is specified |
| Expiration and garbage collection | `design/007-basic-expiration.md` |
| Tag deletion, and why a deleted name is never reusable | `design/006-tag-delete.md` |
| One manifest per array, and the chunk-space bounding box | `design/005-manifest-split.md` — with `PROFILE.md` §D10, which says how much of its "## Design" this release implements |
| Tags from the user's side, including "once deleted, never recreated" | `guide/icechunk-python-version-control.md` → "## Tags" |
| That a repository cannot be created where a store already exists | `guide/icechunk-python-configuration.md` → "### Creating a new repo" |

`PROFILE.md` beside this file records the benchmark's resolutions where these
documents leave behaviour open, disagree with one another, or have gone stale.
Read it after `design/004`, `design/005` and `design/007`: it says which document
wins, and — for `design/005` — which part of a document is a plan rather than
this release.

## The normative specification

| File | What it defines |
| --- | --- |
| `spec.md` | **The Icechunk specification at this release.** Storage operations, the read and write paths, the file layout (`refs/`, `snapshots/`, `attributes/`, `manifests/`, `chunks/`), reference files and their JSON shape, the branch sequence-number encoding, tags, snapshot files, chunk manifest files, chunk files, and the algorithms for initialising, reading, writing and tagging. |
| `guide/spec.md` | A byte-identical duplicate of `spec.md` — upstream publishes the same document at two paths. Reading either is reading both. |
| `archive/spec-alpha1.md` | The original long spec-1 document from `icechunk-v0.1.0-alpha.1`, kept for milestone 1's history. Where it disagrees with `spec.md`, `spec.md` wins. |

`spec.md` is **byte-identical to the previous milestone's**: it gained nothing
at this release, and in several places it is now behind the implementation —
no file-header section, tags still described as undeletable, stale embedded
structs, and a file layout missing two prefixes. See `PROFILE.md` §D1–§D4.

## The design documents

Design documents are where this release's new behaviour is actually written
down. The four numbered 004–007 are new at this release.

| File | What it defines |
| --- | --- |
| `design/001-virtual-chunk-improvements.md` | Virtual chunk containers, credentials, and the global `config.yaml` persisted per repository. Milestone 2. |
| `design/002-rebase-commit-improvements.md` | Commit conflicts, the conflict error's expected/actual parent, `rebase`, and the solver interface. Milestone 2. |
| `design/003-python-instantiation-api.md` | The `Repository` / `Session` / `Store` split and `save_config`. Milestone 2. |
| `design/004-binary-format.md` | **The on-disk binary format**: the 12 magic bytes, the 24-byte implementation id, the spec-version, file-type and compression bytes, the compressed MessagePack payload, and which files carry no header at all. |
| `design/005-manifest-split.md` | Why one manifest per snapshot is too slow, and the move to one manifest per array with a bounding box. Its "## Design" section then goes on to sketch *manifest sets*, assignment rules and a `chunk-manifests:` YAML configuration whose **default packs small arrays together** — none of which exists at this release. Read it with `PROFILE.md` §D10, which says which sentence of that section is the requirement. |
| `design/006-tag-delete.md` | Logical tag deletion by tombstone, parallel resolution of the live and tombstone keys, and why a deleted tag name can never be reused. |
| `design/007-basic-expiration.md` | What expiration and garbage collection are, the "Update `A` in place" algorithm and why it was chosen, the worked example over a six-reference repository, the forbidden expiration operations, and the rules about timestamps. |

## The prose guide

Published documentation. Useful for intent and for the user-facing spelling of a
behaviour; the Python API names in it are **not** the Rust ones.

| File | What it covers |
| --- | --- |
| `guide/icechunk-python-version-control.md` | Snapshots, `ancestry`, time travel, branches, and tags including deletion. 469 lines at this release, up from a 6-line stub. |
| `guide/icechunk-python-configuration.md` | Storage backends, creating and opening repositories, `open_or_create`. Its "## Repository Config" section was replaced at this release by "under construction", so the configuration defaults live in `TEST_INTERFACE.md` instead. |
| `guide/icechunk-python-parallel.md` | Distributed writes, and why pickling a writable session is opt-in. New at this release. |
| `guide/icechunk-python-faq.md` | Why pickling is opt-in. New at this release. |
| `guide/icechunk-python-quickstart.md` | The end-to-end tour: create, write, commit, read history. |
| `guide/icechunk-python-concurrency.md` | Still a stub at this release (headings only). |
| `guide/icechunk-python-virtual.md` | Virtual chunk references from a user's point of view. |
| `guide/icechunk-python-xarray.md`, `guide/icechunk-python-dask.md` | Integrations. Background only. |
| `guide/icechunk-python-index.md`, `guide/index.md`, `guide/overview.md` | Landing pages and the project overview. |
| `guide/icechunk-rust.md` | Fourteen lines saying the Rust library is used internally by the Python one. |
| `guide/faq.md` | The project-level FAQ. |
| `guide/sample-datasets.md` | Marked "under construction" upstream. |
| `guide/icechunk-python-developing.md` | **Superseded.** Milestone 01's copy, left behind by the overlay; upstream dropped it after that release. Eighty lines on building the Python package with `maturin`. Nothing in it is normative here — see `PROFILE.md` §D11. |

## Navigating `spec.md`

| Looking for | Section |
| --- | --- |
| What storage must guarantee | "### Storage Operations" |
| The read and write paths, end to end | "### Overview", "## Algorithms" |
| Which prefix each kind of object lives under | "### File Layout" |
| Branches, tags, and the JSON a reference file holds | "### Reference Files", "#### Creating and Updating Branches", "#### Tags" |
| The branch sequence-number encoding and why the tip sorts first | "#### Creating and Updating Branches" |
| What a snapshot file records | "### Snapshot Files" |
| The three chunk reference kinds | "### Chunk Manifest Files" |
| What may live inside a chunk file | "### Chunk Files" |
| Initialising, reading from a snapshot / branch / tag, writing, tagging | "## Algorithms" |
