# Step 2 — Sessions, conflicts and virtual chunk containers

## 1. Implement this milestone

Turn the single-session store you built in milestone 1 into the three-entity
API of `icechunk` 0.1.0-alpha.8, in the `icechunk` crate at `/app/icechunk`:

- **the `Repository` / `Session` / `Store` split** — a repository is opened or
  created over a storage backend and owns references and configuration; a
  session is the unit of work, writable from a branch or read-only at a branch,
  tag or snapshot; a store is the Zarr-facing key/value view over one session.
  The whole data API moves off `Repository` and onto `Session`.
- **persistent repository configuration** — one configuration object per
  repository, written conditionally so a stale writer cannot clobber a newer
  one, read back at instantiation, and overridable by the caller for a single
  instance without being written back.
- **commit conflicts and rebase** — a commit whose branch tip moved fails
  before writing anything and says which parent it expected and which it found;
  `rebase(solver)` then fast-forwards the intervening commits oldest first,
  either resolving each by policy or stopping with a classified report naming
  the commit it stopped at and the conflicts it found.
- **the conflict classes themselves**, and two solvers: one that only detects,
  and one parameterised by what to do with a doubly-written chunk, doubly-set
  user attributes, and a delete of something the other writer touched.
- **virtual chunk containers** — named URL prefixes, each with an object-store
  configuration, chosen most-specific-first when a virtual chunk is read, with
  credentials supplied per container name, plus optional per-reference
  checksums (an ETag or a last-modified time) that refuse a chunk whose backing
  object has changed.
- **`all_virtual_chunk_locations`**, **`reset_branch`**, the read-only state
  surviving serialisation, and the distributed write path: several sessions
  marshalled to bytes, merged into one, committed once.

Everything milestone 1 established stays required: branches and tags over
snapshots, chunk manifests, the three chunk reference kinds, the change set,
and the Zarr store. It is required **through this release's spelling** — the
module moves, renames and signature changes are listed in section 5 of
`/app/TEST_INTERFACE.md`, and that section is exhaustive. The inherited
behaviour is re-exercised here as well.

The hard prerequisite for step 3 is that sessions, conflicts and rebase work,
because step 3's expiration and garbage collection rewrite exactly the history
those produce.

## 2. Read these local references

**Normative semantics — `/app/icechunk-spec/`:**

- `spec.md` — the specification at this release. "Reference Files", "Creating
  and Updating Branches" and "Tags" first, then "Snapshot Files", "Chunk
  Manifest Files", "Chunk Files", "File Layout" and "Algorithms".
- `design/002-rebase-commit-improvements.md` — **the source of record for the
  design of commit conflicts and rebase.** Read "Proposal" in full. It does not
  enumerate the conflict classes: that list is `PROFILE.md` §D2, and the exact
  Rust spelling — which variant carries which fields, and which solver knob
  repairs which class — is section 4.4 of `/app/TEST_INTERFACE.md`.
- `design/001-virtual-chunk-improvements.md` — **the source of record for
  virtual chunk containers, checksums, per-container credentials and the global
  repository configuration.** Read "Checksum", "Virtual chunk containers",
  "Global config", "Setting virtual references", "New virtual chunk
  datastructures", "Reading virtual chunks", "Default `VirtualChunkContainers`",
  both "Credentials" sections, and "New functions".
- `design/003-python-instantiation-api.md` — repository instantiation, and
  `save_config` as an explicit, commit-less, destructive operation.
- `guide/icechunk-python-quickstart.md` — the milestone end to end, including
  the sentence about what a session becomes once it has committed.
- `guide/icechunk-python-configuration.md` — "Writing chunks inline",
  "Creating a new repo", "Opening an existing repo". Its "Virtual Reference
  Storage Config" section is stale; `PROFILE.md` §D1 says what replaced it.
- `guide/icechunk-python-dask.md` — the distributed write path's constraints.
- `archive/spec-alpha1.md` — the specification milestone 1 was built against,
  for comparison only. Where it disagrees with `spec.md`, `spec.md` wins;
  `PROFILE.md` §R1 lists the differences.

The two pages you would expect to carry this milestone — `guide/icechunk-python-concurrency.md`
and `guide/icechunk-python-version-control.md` — are empty stubs at this
release. The conflict taxonomy they would have held is enumerated in
`/app/icechunk-spec/PROFILE.md` §D2 instead; read it.

**Additional files:**

- `/app/TEST_INTERFACE.md` — the public contract: crate identity, build
  command, every module path, type, signature and format the crate must
  expose, the crates its callers link against, section 5's exhaustive migration
  from milestone 1, section 8's error categories, and section 9's list of what
  is **out of scope**. Read it before you design.
- `/app/WORKSPACE_HANDOFF.md` — what `/app` carries between milestones and what
  must still be there when you finish.
- `/app/icechunk-spec/README.md` — how to navigate the corpus, file by file.
- `/app/icechunk-spec/PROFILE.md` — the benchmark's resolutions where the
  documents leave behaviour open or have fallen behind the code: storage and
  Zarr scope, ordering, error reporting, what milestone 1 left behind (§R1 the
  specification divergences this release retires, §R2 the one milestone-1
  document still on disk that is not part of this corpus), and six named
  doc-versus-implementation drifts.
- `/app/icechunk-spec/LICENSE`, `NOTICE`, `SHA256SUMS` — provenance. Never edit
  a file in `icechunk-spec/`.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  manifests, lockfile, documents and handoff files. Do not create a prose
  answer or an alternate entry point; do not move the crate out of
  `/app/icechunk` and do not rename the package.
- This environment has no network access, and everything the build
  needs is already installed. The pinned dependency set in
  `/app/Cargo.lock` is pre-cached in the image and is the whole
  resolvable crate graph; do not add dependencies. Do not search for, download, copy, wrap, or
  invoke an online implementation or task-specific answer; do not inspect
  `/tests`, later-step directories, or solution material.
- Do not edit the supplied documents (`TEST_INTERFACE.md`, `icechunk-spec/`) or weaken, filter, rename, or disable tests.
  Your crate must work when driven from a separate crate through its public
  API; your own tests are not part of the deliverable.
- Local checks before you finish: `cargo build` in `/app` succeeds,
  `grep '^name' /app/icechunk/Cargo.toml` prints `name = "icechunk"`, and
  `Cargo.lock` is consistent with the manifests. Leave the workspace so a fresh
  session can run the checkpoint commands in `/app/WORKSPACE_HANDOFF.md`
  unchanged.
