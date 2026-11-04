# Step 1 — Iceberg table format core

## 1. Implement this milestone

Build `iceberg`, a Rust library crate at `/app`, implementing the core of the
Apache Iceberg table format at format versions **1 and 2**:

- the Iceberg **type system** — primitives, `struct`, `list`, `map` — and its
  JSON serialisation;
- **schemas** with field IDs, dotted nested names, identifier fields, and the
  Appendix C JSON form;
- **partition specs**, the eight **partition transforms** with their result
  types and ordering properties, and the partition tuple a spec induces over a
  schema;
- **sort orders**;
- **table metadata**: parsing and re-serialising both the v1 and the v2
  document, with schemas, partition specs, sort orders, snapshots, the
  snapshot log, the metadata log and refs;
- **snapshots** and their summaries;
- **manifest files and manifest lists** as Avro object container files, written
  and read back through the crate's own writer and reader;
- **single values** in their Appendix D JSON and binary forms;
- **`FileIO`** over the local filesystem, which every file the crate writes
  goes through.

There is no earlier behaviour to preserve: this is the first milestone. The
hard prerequisite for Step 2 is that table metadata is readable, a manifest and
a manifest list round-trip through your writer and reader, and the partition
transforms parse and type correctly — Step 2's scan planner is built directly
on those.

`cargo build` in `/app` must succeed and the package must stay named `iceberg`.

## 2. Read these local references

**Normative semantics — `/app/iceberg-spec/`:**

- `spec.md` — the Apache Iceberg table specification. Start at
  "Format Versioning", then "Schemas and Data Types", "Identifier Field IDs",
  "Partitioning", "Partition Transforms", "Bucket Transform Details",
  "Truncate Transform Details", "Sorting", "Manifests",
  "Manifest Entry Fields", "Sequence Number Inheritance", "Snapshots",
  "Manifest Lists", "Snapshot References", "Table Metadata",
  "Table Metadata Fields",
  "Appendix A: Format-specific Requirements" (especially its **Avro**
  subsection and its decimal byte-width rule; its **Field IDs** table is out of
  scope at this milestone — `TEST_INTERFACE.md` section 8),
  "Appendix C: JSON serialization", "Appendix D: Single-value serialization"
  and "Appendix E: Format version changes".

`iceberg-spec/PROFILE.md`, listed under **Additional files** below, is
normative too: read it before you design.

**Additional files:**

- `/app/TEST_INTERFACE.md` — the public contract: crate identity, build
  command, every module path, type, signature and format your crate must
  expose, the crates that code using it links against, the migration from the
  previous milestone, and section 8's list of what is **out of scope**. Read it
  before you design.
- `/app/WORKSPACE_HANDOFF.md` — what `/app` carries between milestones and what
  must still be there when you finish.
- `/app/iceberg-spec/README.md` — how to navigate the specification corpus,
  with a section index.
- `/app/iceberg-spec/PROFILE.md` — the benchmark's resolutions where the
  specification leaves behaviour open: format versions in scope, catalog and
  storage scope, data file formats, undefined ordering, timestamps, error
  reporting, the Avro codec, decimal spelling and reserved field ids.
- `/app/iceberg-spec/LICENSE`, `NOTICE`, `SHA256SUMS` — provenance. Never edit
  a file in `iceberg-spec/`.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  manifest, lockfile, documents and handoff files. Do not create a prose answer
  or an alternate entry point; do not move the crate into a subdirectory or a
  workspace member, and do not rename the package.
- This environment has no network access, and everything the
  build needs is already installed. The pinned dependency set in
  `/app/Cargo.lock` is pre-cached in the image and is the whole resolvable
  crate graph; do not add dependencies. Do not search for, download, copy,
  wrap, or invoke an online implementation or task-specific answer; do not
  inspect `/tests`, later-step directories, or solution material.
- Do not edit the supplied documents (`TEST_INTERFACE.md`, `iceberg-spec/`) or weaken, filter, rename, or
  disable tests. Separate drivers are compiled against your crate, and yours
  are never run.
- Local checks before you finish: `cargo build` in `/app` succeeds,
  `grep '^name' /app/Cargo.toml` prints `name = "iceberg"`, and `Cargo.lock` is
  consistent with `Cargo.toml`. Leave the workspace so a fresh session can run
  the checkpoint commands in `/app/WORKSPACE_HANDOFF.md` unchanged.
