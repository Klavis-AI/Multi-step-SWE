# Step 1 — The spec-1 Icechunk repository format

## 1. Implement this milestone

Build `icechunk`, a Rust library crate at `/app/icechunk`, implementing the
transactional repository format Icechunk defines for Zarr on object storage:

- a **repository** over a pluggable object store, initialised once and then read
  and written through snapshots — with in-memory and local-filesystem backends
  that both offer conditional create-if-not-exists;
- **snapshots**: every commit writes a new one recording the whole node
  hierarchy — groups, arrays, their Zarr metadata and their user attributes —
  and the chunk references reachable from it;
- the **three chunk reference kinds**: bytes inlined in the manifest, a
  reference to a chunk the repository wrote, and a virtual reference into a
  pre-existing file;
- **references**: a mutable branch whose every update is an atomic
  compare-and-swap against its current tip, and an immutable tag; `main` is the
  branch a new repository starts on;
- **version control**: commit, ancestry, opening a repository at a snapshot id,
  at a branch tip or at a tag, branching, and the conflict a second writer gets
  when the branch has moved under it;
- a **change set** a distributed writer can export and a coordinator can merge
  into its own commit;
- a **Zarr v3 store** over all of it: `zarr.json` metadata keys and `c/i/j/k`
  chunk keys, get with byte ranges, set, delete, the three listings, checkout,
  reset, branch, tag and commit.

There is no earlier behaviour to preserve: this is the first milestone. The
workspace starts with a manifest and an empty crate root and no implementation.

`cargo build` in `/app` must succeed and the package must stay named `icechunk`.

## 2. Read these local references

**Normative semantics — `/app/icechunk-spec/`:**

- `spec.md` — the Icechunk specification. Start at "Storage Operations" and
  "Overview", then "Reference Files", "Creating and Updating Branches", "Tags",
  "Snapshot Files", "Chunk Manifest Files", "Chunk Files", and the "Algorithms"
  sections: "Initialize New Repository", "From Snapshot ID", "From Branch",
  "From Tag", "Write New Snapshot" and "Create New Tag".
- `PROFILE.md` — **normative, and it overrides `spec.md` where they conflict.**
  This release ships a format that differs from its own specification in seven
  concrete places — the object id length, the file layout, the branch key
  grammar, the tag key grammar, what a reference file contains, the encoding of
  snapshots and manifests, and attributes files — and `PROFILE.md` resolves
  every one of them (P1 to P7), then settles what the specification simply
  leaves open (P8 to P13): the inline-chunk threshold and its default, the
  storage backends in scope, virtual references, the Zarr metadata subset,
  error granularity and ordering. Read it before you design.
- `guide/index.md` — the project's own prose on what the format is for and what
  it guarantees. `guide/icechunk-python-index.md` and
  `guide/icechunk-python-developing.md` are background only; the Python binding
  is not part of this milestone.

**Additional files:**

- `/app/TEST_INTERFACE.md` — the public contract: crate identity, the build
  command, every module path, type, signature, format and error category the
  crate must expose, the crates its callers link against, and section 8's list
  of what is **out of scope**. Read it before you design.
- `/app/WORKSPACE_HANDOFF.md` — what `/app` carries between milestones and what
  must still be there when you finish.
- `/app/icechunk-spec/README.md` — how to navigate the corpus, with a section
  index.
- `/app/icechunk-spec/LICENSE`, `NOTICE`, `SHA256SUMS` — provenance. Never edit
  a file in `icechunk-spec/`.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied manifests,
  lockfile, documents and handoff files. Do not create a prose answer or an
  alternate entry point; do not move the library out of `/app/icechunk` and do
  not rename the package.
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
