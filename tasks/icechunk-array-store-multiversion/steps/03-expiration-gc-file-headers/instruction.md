# Step 3 — Expiration, garbage collection and file headers

## 1. Implement this milestone

Take the `icechunk` crate from a repository that only ever grows to one that can
forget, and give its metadata files a real on-disk format:

- a **binary header** on every snapshot, manifest, attributes and
  transaction-log file — magic bytes, the writing client's id, the spec version,
  the file type and the compression algorithm — with the payload behind it
  **Zstd-compressed**, and chunk, reference and configuration files left
  unheadered;
- **expiration**: rewriting history in place so that snapshots older than a
  caller-supplied cutoff drop out of every branch's and tag's ancestry, without
  deleting a single object;
- **garbage collection**: removing exactly the objects no branch or tag can
  reach — snapshots, their exclusive manifests, their exclusive chunks and their
  transaction logs — and reporting a per-category count of what went;
- **tag deletion**, after which the tag stops resolving, stops being listed, and
  its name can never be used again;
- **refusing to create a repository in a prefix that is not empty**;
- **arbitrary properties on a commit**, readable back through `ancestry`, which
  now takes a branch, a tag or a snapshot id and yields `SnapshotInfo`;
- **one chunk manifest per array**, each recorded in the snapshot with the
  chunk-space bounding box it covers, its size and its row count, plus
  `Session::chunk_coordinates` to list an array's initialized chunks;
- **field-by-field configuration merge**, with every `RepositoryConfig` field
  optional and read through getters, over the configuration persisted in the
  repository;
- the `asset_manager` module that replaces the previous milestone's
  `MemCachingStorage` and threads storage settings through the `Storage` trait.

Everything milestones 1 and 2 established stays required: the spec-1 repository
format with its three chunk reference kinds, and sessions, commit conflicts,
rebase and virtual chunk containers. Both are re-exercised here. Where this
milestone changes an earlier signature — and it changes many — the change is
listed in `/app/TEST_INTERFACE.md` section 11 and the new form is what is
required.

The hard prerequisite for Step 4 is that expiration and garbage collection work
through the public `ops::gc` surface, because Step 4 replaces the serialization
format underneath them without changing what they do.

## 2. Read these local references

**Normative semantics — `/app/icechunk-spec/`:**

- `design/004-binary-format.md` — the whole document. It is the **only** place
  the file header is specified; `spec.md` has no header section.
- `design/007-basic-expiration.md` — "What are expiration and garbage
  collection", "Left algorithm: Update A in place", "Comparison between the
  algorithms", "Using this algorithm for snapshot expiration", "Forbidden
  Expiration Operations", the worked example, and "On timestamps".
- `design/006-tag-delete.md` — "The problems", "Tag delete", "Design".
- `design/005-manifest-split.md` — "Current state", "What we want", and the
  **first paragraph** of "Design" ("Each array will go fully into a single
  manifest"). The rest of that section — manifest sets, assignment rules, the
  `chunk-manifests:` YAML and its default configuration, the packing algorithm —
  is a plan for a later release and is **not implemented and out of scope
  here**;
  `PROFILE.md` §D10 says so explicitly, and `TEST_INTERFACE.md` section 9.2 is
  normative for what this milestone does require.
- `spec.md` — "File Layout", "Reference Files", "Creating and Updating
  Branches", "Tags", "Snapshot Files", "Chunk Manifest Files", "Chunk Files" and
  "Algorithms". These are milestones 1's and 2's sections and are named again
  because they are re-exercised here. `spec.md` is byte-identical to the
  previous milestone's, and four of its statements are now behind the
  implementation; `PROFILE.md` §D1–§D4 says which and why.
- `guide/icechunk-python-version-control.md` — "## Tags" for tag deletion, and
  "## Setup" for what `ancestry` returns.
- `guide/icechunk-python-configuration.md` — "### Creating a new repo" for the
  clean-prefix rule.
- `design/001-virtual-chunk-improvements.md` and
  `design/002-rebase-commit-improvements.md` — milestone 2's sections, named
  again because virtual chunk containers, commit conflicts
  and rebase are re-exercised here.

**Additional files:**

- `/app/TEST_INTERFACE.md` — the public contract: crate identity, build command,
  every module path, type, signature, constant and byte layout the crate must
  expose, the crates its callers link against, the inherited contract in this
  release's spelling, the migration from milestone 2, and section 12's list of
  what is **out of scope**. Read it before you design.
- `/app/WORKSPACE_HANDOFF.md` — what `/app` carries between milestones and what
  must still be there when you finish.
- `/app/icechunk-spec/README.md` — how to navigate the corpus, with a per-file
  index and a table of where this milestone's behaviour is written down.
- `/app/icechunk-spec/PROFILE.md` — the benchmark's resolutions where the
  documents leave behaviour open or disagree. `P*` carries forward the standing
  resolutions (backends in scope, error categories, undefined ordering,
  timestamps, the inline-chunk threshold, attribute files) with the two this
  release amends; `R*` records what it **retires** — configuration override is
  now per field rather than wholesale, and tags became deletable; `D*` names the
  drifts in this release's own corpus, including where the file header is
  specified, which parts of `design/005` and `design/007` are out of scope, and
  which one guide page in the directory is superseded.
- `/app/icechunk-spec/LICENSE`, `NOTICE`, `SHA256SUMS` — provenance. Never edit
  a file in `icechunk-spec/`.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  manifests, lockfile, documents and handoff files. Do not create a prose answer
  or an alternate entry point; do not move the crate out of `/app/icechunk`, do
  not add a second default workspace member, and do not rename the package.
- This environment has no network access, and everything the build
  needs is already installed. The pinned dependency set in
  `/app/Cargo.lock` is pre-cached in the image and is the whole
  resolvable crate graph; do not add dependencies. Do not search for, download, copy, wrap, or invoke
  an online implementation or task-specific answer; do not inspect `/tests`,
  later-step directories, or solution material.
- Do not edit the supplied documents (`TEST_INTERFACE.md`, `icechunk-spec/`) or weaken, filter, rename, or disable tests.
  Your crate must work when driven from a separate crate through its public
  API; your own tests are not part of the deliverable.
- Local checks before you finish: `cargo build` in `/app` succeeds,
  `grep '^name' /app/icechunk/Cargo.toml` prints `name = "icechunk"`, and
  `Cargo.lock` is consistent with the manifests. Leave the workspace so a fresh
  session can run the checkpoint commands in `/app/WORKSPACE_HANDOFF.md`
  unchanged.
