# Step 2 — Scan planning and pruning

## 1. Implement this milestone

Add **scan planning** to the `iceberg` crate you built in milestone 1:

- a public `scan` module: a table scan that resolves a snapshot, reads its
  manifest list and manifests, and produces the deterministic set of file scan
  tasks a reader must process;
- **expression binding**: unbound predicates over column references bound to a
  schema, with `NOT` rewritten away, and the documented failure when a
  reference names no column;
- **partition pruning** — evaluating a bound predicate against a manifest's
  partition field summaries so that manifests which cannot contain a matching
  row are not opened;
- **column-statistics pruning** — evaluating a bound predicate against a data
  file's lower bounds, upper bounds, null counts and NaN counts;
- **inclusive projection** of a predicate over a partition spec;
- **transform literals** — applying every partition transform to a single
  value, which is where the specification's published bucket hash test vectors
  and truncation examples become observable;
- **Iceberg-to-Arrow schema conversion**, carrying field ids;
- **`FileIO` schemes**, so a location is resolved by its URI scheme;
- **view metadata**, per the view specification.

Everything milestone 1 established stays required: the type system, schemas,
partition specs, sort orders, table metadata v1 and v2, snapshots, and the
Avro manifest and manifest-list readers and writers. Where this milestone
changes a milestone-1 signature, the change is listed in
`/app/TEST_INTERFACE.md` section 5 and the new form is what is required —
section 5 lists every such change.

The hard prerequisite for Step 3 is that a scan plans files from real
manifests and that predicates bind and prune, because Step 3's metadata
algebra is exercised through the same table.

## 2. Read these local references

**Normative semantics — `/app/iceberg-spec/`:**

- `spec.md` — "Scan Planning" first, then "Schemas and Data Types",
  "Partitioning", "Partition Transforms", "Bucket Transform Details",
  "Truncate Transform Details", "Partition Evolution", "Manifests",
  "Manifest Entry Fields", "Manifest Lists", "Sequence Number Inheritance",
  "Column Projection" (a sub-subsection of "Schemas and Data Types", not in
  the README's navigation index), "File System Operations" (likewise not in
  that index — it is what section 4.9 of `/app/TEST_INTERFACE.md` cites),
  "Appendix A: Format-specific Requirements",
  "Appendix B: 32-bit Hash Requirements", "Appendix C" and "Appendix D".
- `view-spec.md` — view metadata, view versions and view representations.

Appendix A's **Field IDs** table is about the Avro `field-id` attributes, not
about Arrow: the Arrow field-id metadata key this milestone must emit is given
in `/app/TEST_INTERFACE.md` section 4.7.

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
