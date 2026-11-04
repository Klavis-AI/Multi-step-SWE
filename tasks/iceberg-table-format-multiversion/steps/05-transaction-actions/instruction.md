# Step 5 — Transactions and commits

## 1. Implement this milestone

Add **transactional commits** to the `iceberg` crate:

- a **catalog** held entirely in memory over a local-filesystem `FileIO`:
  namespaces, table creation, loading, listing, renaming and dropping, with the
  documented failures;
- a **transaction** that owns the table it was built from and produces the
  updates and requirements a commit carries;
- **commit actions**: a fast append that produces a new snapshot with the
  correct parent, sequence number and summary; property updates; a location
  update; a format-version upgrade; a sort-order replacement; and a statistics
  update;
- **optimistic concurrency** — two transactions built from the same base table
  must not both succeed: the second fails its requirement once the first has
  committed, exactly as the specification's conflict-resolution section
  requires;
- **manifest-list field summaries** whose bounds are the specification's byte
  encoding of a single value, and delete manifests tracked by a manifest list.

Everything milestones 1–4 established stays required: the table format core,
scan planning, the metadata update algebra, and row-level deletes. The gate
re-exercises all four. This milestone also moves the crate's package version
to `0.6.0`, read out of `/app/Cargo.toml`, which `/app/TEST_INTERFACE.md`
section 1 pins; the Rust edition stays `2024`. Where this milestone changes an
earlier signature — and it changes several — the change is listed in
`/app/TEST_INTERFACE.md` section 5 and the new form is what is required.

This is the final milestone.

## 2. Read these local references

**Normative semantics — `/app/iceberg-spec/`:**

- `spec.md` — "Optimistic Concurrency" and
  "Commit Conflict Resolution and Retry" first, then "File System Tables",
  "Metastore Tables", "Snapshots", "Sequence Numbers", "Snapshot References",
  "Snapshot Retention Policy", "Manifest Lists", "Manifests",
  "Table Metadata Fields", "Table Statistics", "Sorting",
  "Appendix D: Single-value serialization" (its **Bound serialization**
  subsection), "Appendix E: Format version changes" and
  "Appendix F: Implementation Notes" (its **Optional Snapshot Summary Fields**
  subsection, which names the summary metrics and the other fields).

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
