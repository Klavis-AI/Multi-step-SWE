# Icechunk specification corpus

Verbatim copies of the `earth-mover/icechunk` documents, pinned at the release
this milestone is anchored to. **These files contain no implementation** — they
are documentation and nothing else. They are read-only inputs: never edit, trim,
annotate or extend them, and never add a file to this directory.

| Edition | `earth-mover/icechunk` tag `icechunk-v0.1.0-alpha.1`, commit `e20f7e8663eedb737c712cdbcceeec21cc3b1b2e` |
| --- | --- |
| Source paths | `spec/` and `docs/docs/` in that repository |
| Licence | Apache-2.0 — see `LICENSE` and `NOTICE` in this directory |
| Integrity | `SHA256SUMS` in this directory covers every file below except `README.md` and `PROFILE.md` |

`README.md` (this file) and `PROFILE.md` are **task-owned**, not upstream, and
are therefore not listed in `SHA256SUMS`.

## The files in this directory

| File | What it is |
| --- | --- |
| `spec.md` | **The normative Icechunk specification** at this release, 1094 lines. Everything the format requires. |
| `PROFILE.md` | **Task-owned and normative.** The benchmark's resolutions where `spec.md` leaves behaviour open — and, for this release, the seven places (P1–P7) where `spec.md` and the format it ships alongside disagree. Where the two conflict, `PROFILE.md` wins. Read it before you design. |
| `guide/index.md` | The project's front page: what Icechunk is for, the feature list, and the concurrency/consistency claims in prose. |
| `guide/icechunk-python-index.md` | The Python quickstart. Background only: the Python binding is not part of this milestone. |
| `guide/icechunk-python-developing.md` | How the project builds and tests itself. Background only. |
| `LICENSE`, `NOTICE` | Provenance for everything above. |

## Navigating `spec.md`

One long document. These are the sections this milestone needs.

| Looking for | Section |
| --- | --- |
| What the format is for and what it deliberately is not | "Introduction", "Goals", "Non Goals" |
| What a repository requires of its object store — conditional create, read-after-write, listing, deletes | "Storage Operations" |
| How a commit, a snapshot, a manifest and the chunks fit together; the read path and the write path | "Overview" |
| Where files live beneath the root (but see `PROFILE.md` P2) | "File Layout", "File Formats" |
| Branches and tags: what each is, and where atomicity is enforced | "Reference Files" |
| The branch sequence, and how a commit races (but see `PROFILE.md` P3) | "Creating and Updating Branches" |
| Tag immutability and create-if-not-exists (but see `PROFILE.md` P4) | "Tags" |
| What a snapshot records: nodes, node metadata, properties, parents | "Snapshot Files" |
| Why attributes files are not implemented | "Attributes Files" |
| The three chunk reference kinds — inline, `Ref`, `Virtual` — and their fields | "Chunk Manifest Files" |
| What a chunk file may be, including a foreign file | "Chunk Files" |
| Initialising a repository | "Initialize New Repository" |
| Opening at a snapshot id, at a branch tip, at a tag | "Read from Repository", "From Snapshot ID", "From Branch", "From Tag" |
| Committing: the ordered steps and the retry | "Write New Snapshot" |
| Creating a tag from any snapshot | "Create New Tag" |
| How the design compares with Apache Iceberg | "Comparison with Iceberg" |

`spec.md` lines 251–869 and 889–1010 are embedded JSON Schema blocks for the
snapshot and manifest files — the "Snapshot Files" and "Chunk Manifest Files"
sections are almost entirely schema. `PROFILE.md` P6 explains what is and is not scored
about them; read the prose around them rather than the schemas.

The public Rust surface this milestone must expose is **not** in this directory.
It is in `/app/TEST_INTERFACE.md`.
