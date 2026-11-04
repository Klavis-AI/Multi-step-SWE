# Step 4 — Format-v2 row-level deletes

## 1. Implement this milestone

Add **row-level deletes** to the `iceberg` crate:

- **position delete files** and **equality delete files** as first-class
  manifest content: delete manifests, the delete content types, the reserved
  column ids the specification assigns to the position-delete columns, and the
  equality field ids an equality delete file carries;
- **sequence numbers and their inheritance**, so that a delete file applies to
  exactly the data files the specification says it applies to;
- **Puffin** blob files: writing a file with blobs and reading its footer back,
  per the Puffin specification, including the blob type the specification
  assigns to a **deletion vector** and the data-file fields that reference a
  deletion vector's location inside a Puffin file;
- **metadata inspection tables** over a table's snapshots and manifests;
- an **object cache** for parsed manifests and manifest lists.

Everything milestones 1–3 established stays required: the table format core,
scan planning, and the metadata update algebra. The gate re-exercises all
three. This milestone also moves the crate to **Rust edition 2024** and its
package version to `0.5.1`; both are read out of `/app/Cargo.toml`, which
`/app/TEST_INTERFACE.md` section 1 pins. Where this milestone changes an
earlier signature, the change is listed in `/app/TEST_INTERFACE.md` section 5
and the new form is what is required — read that table before reusing a
milestone-3 signature, because one of milestone 2's scan accessors changes
here.

The specification is normative for *semantics*, but where its per-version
columns and `/app/TEST_INTERFACE.md` section 4 disagree about which fields a
format-2 manifest carries, **section 4 governs**: it says which fields this
milestone requires and why, under the format-version carve-out in
`/app/iceberg-spec/PROFILE.md` P1. Section 4.2 is the one place that happens.

The hard prerequisite for Step 5 is that a delete file round-trips through a
delete manifest, because Step 5's transactions commit them.

## 2. Read these local references

**Normative semantics — `/app/iceberg-spec/`:**

- `spec.md` — "Row-level Deletes" and "Delete Formats" first, then
  "Position Delete Files", "Equality Delete Files", "Identifier Field IDs",
  "Deletion Vectors", "Delete File Stats", "Sequence Numbers",
  "Sequence Number Inheritance", "Manifests", "Manifest Entry Fields",
  "Manifest Lists", "Reserved Field IDs", "Snapshots", "Scan Planning",
  "File System Operations", "Appendix A: Format-specific Requirements" and
  "Appendix E: Format version changes".
- `puffin-spec.md` — the Puffin file format: magic, footer struct, blob
  metadata, and compression codecs.

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
