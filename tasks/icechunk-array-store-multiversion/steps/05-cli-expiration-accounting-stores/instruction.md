# Step 5 — Command line, expiration accounting and stores

## 1. Implement this milestone

Extend the cumulative `icechunk` crate at `/app/icechunk` with:

- an **`icechunk` command-line binary**, gated behind a non-default `cli`
  feature, that reads a YAML configuration file of named repositories and
  offers `repo create`, `snapshot list`, `config init`, `config add` and
  `config list`, with the argument grammar, exit statuses and output shape
  `/app/TEST_INTERFACE.md` section 5 fixes;
- **snapshot lookup**: `Repository::lookup_snapshot`, which answers from
  storage, so it finds a snapshot no reference reaches and fails once garbage
  collection has removed it;
- **storage accounting**: `ops::stats::repo_chunks_storage`, the total bytes of
  the committed chunk objects a repository's references reach — excluding
  uncommitted, inline and virtual chunks — and byte accounting in garbage
  collection, where `GCSummary` counters became `u64` and gained
  `bytes_deleted`;
- **expiration accounting**: `ExpireRefResult` reports `ref_is_expired` on both
  of its two remaining variants — it is the plain `flushed_at < older_than`
  test on the reference's own tip, `true` even when that tip *is* the first
  snapshot — expiration never *releases* the repository's first snapshot, and a
  threshold older than every commit does nothing instead of looping
  (section 4.10.2 states all three);
- **reference validation**: creating a branch or a tag at a snapshot that does
  not exist is refused, and leaves no reference behind;
- **repository-level commit metadata**, merged under each commit's own
  properties, key by key;
- **manifest information on `SnapshotInfo`**, so every snapshot record reports
  the manifests it references, their sizes and their chunk-reference counts;
- the **distributed-write reduction**: marshal several writers' sessions,
  fold their change sets into one coordinator session, commit once;
- **object-store configuration**: `S3Options::force_path_style`, a Cloudflare
  R2 constructor, a Tigris constructor whose consistency mode decides whether
  the backend is writable, and the `unsafe_use_conditional_*` /
  `unsafe_use_metadata` switches on `storage::Settings`.

Everything milestones 1–4 established stays required: the repository format and
the Zarr store, the session and conflict model, expiration and collection, and
the FlatBuffers format objects with their split error types. All four must
keep working. This milestone also moves the crate to edition **2024**
and version **`0.2.11`**. Where it changes an earlier signature — and it
changes many — the change is listed in `/app/TEST_INTERFACE.md` section 6 and
the new form is what is required.

## 2. Read these local references

**Normative semantics — `/app/icechunk-spec/`:**

- `guide/icechunk-python-expiration.md` — what expiration and garbage
  collection promise: `## Expire snapshots`, `## Delete expired data`, and
  `## Generate a few snapshots`.
- `guide/icechunk-python-storage.md` — the per-backend configuration:
  `#### Tigris`, `#### Cloudflare R2`, `#### Minio` (which is where
  `force_path_style` is documented), `### Filesystem Storage` and
  `### In Memory Storage`.
- `design/009-tigris-support.md` — `## How to achieve it`, for what the strong
  and weak consistency modes mean and why the weak one is read-only.
- `guide/icechunk-python-parallel.md` — `## Distributed writes` and
  `### Cooperative distributed writes`, the reduction this milestone's session
  merging implements.
- `spec.md` — the on-disk format, unchanged at this milestone: `### File
  Layout`, `### Reference Files`, `### Snapshot Files`,
  `### Chunk Manifest Files`, `### Chunk Files` and `## Algorithms`.
  `### Chunk Files` lists four ways chunks *may* be arranged in files;
  `/app/TEST_INTERFACE.md` section 4.7 says which one this milestone takes and
  what that means for the chunk reference it records. Read it beside
  `PROFILE.md`'s "Documentation drift carried forward": D1 there records that
  `#### Creating and Updating Branches` describes a sequence-numbered
  branch-file scheme the crate abandoned at milestone 04, and D2–D4 record
  three more places the corpus has not caught up with the code.

**Additional files:**

- `/app/TEST_INTERFACE.md` — the public contract: crate identity, both build
  commands, every module path, type and signature the crate must expose, the
  complete command-line grammar with its exit statuses and output shape, the
  configuration-file syntax, the migration from milestone 04, and section 9's
  list of what is **out of scope**. Read it before you design. Four statements are
  easy to miss because they are stated away from the type they bind: section
  5.5 fixes the **serialised form of the credential enums** of section 4.7
  (internally tagged, `snake_case`); section 4.10.2 settles what
  `ref_is_expired` reports when a reference has never moved off the first
  snapshot; sections 4.7 and 4.10 together fix what a **committed native
  chunk object** holds — one object per chunk, the caller's bytes unframed and
  uncompressed — which is what makes `repo_chunks_storage`'s totals exact; and
  section 3's bullet on `Result::expect_err` requires `std::fmt::Debug` on
  **`Repository`** and **`NodeSnapshot`**, because that call bounds the
  *successful* type of a `Result` rather than the error type.
- `/app/WORKSPACE_HANDOFF.md` — what `/app` carries between milestones and what
  must still be there when you finish.
- `/app/icechunk-spec/README.md` — how to navigate the corpus, with an index of
  which document answers which question.
- `/app/icechunk-spec/PROFILE.md` — the benchmark's resolutions where the
  documents leave behaviour open or contradict each other, carried forward from
  earlier milestones and extended for this one.
- `/app/icechunk-spec/LICENSE`, `NOTICE`, `SHA256SUMS` — provenance. Never edit
  a file in `icechunk-spec/`.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  manifests, lockfile, documents and handoff files. Do not create a prose
  answer or an alternate entry point; do not move the crate out of
  `/app/icechunk`, do not rename the package, and do not drop it from the
  workspace's `default-members`.
- This environment has no network access, and everything the build
  needs is already installed. The pinned dependency set in
  `/app/Cargo.lock` is pre-cached in the image and is the whole
  resolvable crate graph; do not add dependencies. Do not search for, download, copy, wrap, or
  invoke an online implementation or task-specific answer; do not inspect
  `/tests`, later-step directories, or solution material.
- Do not edit the supplied documents (`TEST_INTERFACE.md`, `icechunk-spec/`) or weaken, filter, rename, or disable tests.
  Your crate must work when driven from a separate crate through its public
  API, and your binary when run as a process; your own tests are not part of
  the deliverable.
- Local checks before you finish: `cargo build` in `/app` succeeds,
  `cargo build --manifest-path icechunk/Cargo.toml --features cli --bin icechunk`
  produces `target/debug/icechunk`, `target/debug/icechunk --help` exits 0,
  `grep '^name' /app/icechunk/Cargo.toml` prints `name = "icechunk"`, and
  `Cargo.lock` is consistent with the manifests. Leave the workspace so a fresh
  session can run the checkpoint commands in `/app/WORKSPACE_HANDOFF.md`
  unchanged.
