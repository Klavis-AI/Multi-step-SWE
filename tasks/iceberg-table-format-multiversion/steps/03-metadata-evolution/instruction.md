# Step 3 — Metadata evolution

## 1. Implement this milestone

Add the **table metadata update algebra** to the `iceberg` crate:

- a metadata **builder** that takes an existing table metadata (or a fresh
  table creation) and applies the documented updates: assign a UUID, set the
  location, add a schema and make it current, add a partition spec and make it
  default, add a sort order and make it default, add a snapshot, set and remove
  a snapshot reference, set and remove properties, and upgrade the format
  version;
- the **identifier bookkeeping** those updates imply — new schema ids, new
  partition spec ids, `last-column-id`, `last-partition-id`, the snapshot log
  and the metadata log — exactly as the specification's schema-evolution and
  partition-evolution rules require;
- **table updates and table requirements** as first-class values: each update
  serialises to its documented JSON action, and each requirement can be checked
  against a metadata and fails when it does not hold;
- **nanosecond timestamp types** and their partition-transform behaviour;
- the split between an **unbound** partition spec (source names or ids, no
  schema) and a **bound** one (validated against a schema), and the
  compatibility rules between two specs;
- **schema field-id reassignment**.

Everything milestones 1 and 2 established stays required: the table format
core, and scan planning with predicate binding and pruning. The gate
re-exercises both. Where this milestone changes an earlier signature, the
change is listed in `/app/TEST_INTERFACE.md` section 5 and the new form is what
is required.

The hard prerequisite for Step 4 is that metadata can be built and evolved
through the builder, because Step 4's delete files are committed into a table
whose metadata the builder produces.

## 2. Read these local references

**Normative semantics — `/app/iceberg-spec/`:**

- `spec.md` — "Table Metadata" and "Table Metadata Fields" first, then
  "Schema Evolution", "Identifier Field IDs", "Partition Transforms",
  "Partition Evolution", "Sorting", "Snapshots", "Snapshot References",
  "Sequence Numbers", "Primitive Types" (for `timestamp_ns` and
  `timestamptz_ns`), "Appendix C: JSON serialization",
  "Appendix D: Single-value serialization" (for the 8-byte binary form of a
  nanosecond timestamp) and "Appendix E: Format version changes". Read
  "Snapshot Retention Policy" for the retention **fields** a snapshot reference
  carries; *enforcing* the policy is not part of this milestone
  (`TEST_INTERFACE.md` section 8). "Scan Planning" is milestone 2's section and
  is named again here because the gate re-exercises it; so are "Manifests",
  "Manifest Lists" and "Appendix A: Format-specific Requirements", which are
  milestone 1's sections and define the Avro object-container form
  `TEST_INTERFACE.md` section 6 anchors the manifest and manifest-list files
  to.

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
