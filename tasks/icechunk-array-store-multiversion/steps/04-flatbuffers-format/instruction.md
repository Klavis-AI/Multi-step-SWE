# Step 4 — The FlatBuffers metadata format

## 1. Implement this milestone

Replace Icechunk's metadata encoding with the **FlatBuffers format**, in the
`icechunk` crate at `/app/icechunk`:

- **snapshots, chunk manifests and transaction logs are FlatBuffers buffers**,
  built from the IDL shipped at `/app/icechunk-spec/flatbuffers/`. Each of the
  three types becomes an owned byte buffer behind `bytes()` / `from_buffer()`
  with typed accessors over it, instead of a deserialised Rust structure;
- **verification runs once, on the way in**: `from_buffer` verifies the whole
  buffer and every later accessor trusts it, so a truncated, corrupted or
  foreign payload is a **named error** rather than a panic or an out-of-bounds
  read;
- the **on-disk envelope**: a 39-byte plaintext header — magic, implementation
  id, spec version, file type, compression — followed by a compressed frame
  whose plaintext is the bare FlatBuffers root, with a named diagnostic for
  every way the header can be wrong;
- **transaction logs gain content**: which groups and arrays a commit created,
  deleted or updated, and which chunk coordinates it touched — and with them
  `Repository::diff` and `Session::status`;
- the **`ICError` split**: every error enum becomes a `XxxErrorKind` plus
  `type XxxError = ICError<XxxErrorKind>` carrying a span of context;
- **`ZarrArrayMetadata` and the whole `metadata` module go away.** A node now
  carries the caller's Zarr metadata verbatim as `user_data: Bytes` plus a typed
  `shape` and `dimension_names`, so Zarr extensions and fields Icechunk does not
  model survive a round trip untouched;
- **branch references move** from sequence-numbered files to a single
  `refs/branch.$NAME/ref.json` written with a conditional put — which is what
  makes a repository written by the previous release unreadable here, with a
  clear diagnostic rather than a crash;
- **`Store::set_virtual_refs`**, a bulk virtual-reference setter that takes the
  session lock once and reports the references whose container validation failed
  instead of failing the batch.

Everything milestones 1–3 established stays required: the repository object
layout and its reference kinds; sessions, conflict detection, rebase and virtual
chunk containers; expiration, garbage collection and the 39-byte file header.
All three are re-exercised in this step.

This milestone changes many inherited signatures — `Snapshot`'s accessors,
`Manifest::id`, `Session::add_array`, the storage constructors becoming `async`,
and every error match. Section 7 of `/app/TEST_INTERFACE.md` lists the migration
exhaustively; **read it before reusing a milestone-3 signature.**

Two items in the upstream release notes for this version are **Python-binding
changes with no Rust-side delta** — a lazy `ancestry` iterator and a positional
branch argument to `readonly_session`. `/app/TEST_INTERFACE.md` section 10 says
what is and is not in scope there; do not change the Rust signatures for them.

Where `/app/icechunk-spec/` and `/app/TEST_INTERFACE.md` disagree about
something this release changed, `/app/icechunk-spec/PROFILE.md` records the
resolution and `TEST_INTERFACE.md` governs. Its "Documentation drift at this
release" section lists **four** such points, D1 to D4. Two of them (D1, the
branch reference files; D2, the Msgpack payload) are disagreements this release
created; the other two (D3, swapped schema comments; D4, the unverified `"Ichk"`
identifier) are clarifications. Read all four.

## 2. Read these local references

**Normative semantics — `/app/icechunk-spec/`:**

- `flatbuffers/` — the five `.fbs` schema files: `all.fbs`, `object_ids.fbs`,
  `snapshot.fbs`, `manifest.fbs`, `transaction_log.fbs`. These are the normative
  description of the metadata files.
- `spec.md` — "File Layout", "File Formats", "Reference Files", "Snapshot
  Files", "Attributes Files", "Chunk Manifest Files", "Chunk Files", and the
  "Algorithms" section.
- `design/004-binary-format.md` — the file header and its byte table.
- `design/008-no-copy-serialization-formats.md` — why FlatBuffers, and what the
  format has to make possible.
- `design/007-basic-expiration.md` — expiration and garbage collection, still
  required here.
- `design/001-virtual-chunk-improvements.md`, `design/002-rebase-commit-improvements.md`,
  `design/006-tag-delete.md` — inherited behaviour this step still carries.
- `guide/icechunk-python-version-control.md`, `guide/icechunk-python-virtual.md`,
  `guide/icechunk-python-configuration.md` — the user-facing semantics of
  version control, virtual chunks and configuration. The version-control guide
  is also where the conflict solver's chunk-conflict selection is worked through
  end to end.
- `guide/icechunk-python-parallel.md` — the distributed-write protocol: a worker
  hands its session's changes back, a coordinator merges them, and one commit
  covers them all. This is what `Session::discard_changes` and `Session::merge`
  are for.

**Additional files:**

- `/app/TEST_INTERFACE.md` — the public contract: crate identity, build command,
  every module path, type, signature, on-disk byte layout and error category the
  crate must expose, the crates its callers link against, the migration from the
  previous milestone, and section 10's list of what is **out of scope**. Read it
  before you design.
- `/app/WORKSPACE_HANDOFF.md` — what `/app` carries between milestones and what
  must still be there when you finish.
- `/app/icechunk-spec/README.md` — how to navigate the corpus, with a file index.
- `/app/icechunk-spec/PROFILE.md` — this benchmark's resolutions where the
  corpus is silent, stale or self-contradictory, carried forward from earlier
  milestones and extended for this one.
- `/app/icechunk-spec/LICENSE`, `NOTICE`, `SHA256SUMS` — provenance. Never edit
  a file under `icechunk-spec/`.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  manifests, lockfile, documents and handoff files. Do not create a prose answer
  or an alternate entry point; do not move the crate out of `/app/icechunk`, do
  not flatten the workspace, and do not rename the package.
- This environment has no network access, and everything the build
  needs is already installed. The pinned dependency set in
  `/app/Cargo.lock` is pre-cached in the image and is the whole
  resolvable crate graph; do not add dependencies. Do not search for, download, copy, wrap, or invoke
  an online implementation or task-specific answer; do not inspect `/tests`,
  later-step directories, or solution material.
- Do not edit the supplied documents (`TEST_INTERFACE.md`, `icechunk-spec/`) or weaken, filter, rename, or disable tests.
  Your crate must work when driven from a separate crate through its public
  API; your own tests are not part of the deliverable.
- The FlatBuffers compiler `flatc` is **not installed** in this workspace and
  nothing fetches it for you. `/app/TEST_INTERFACE.md` section 6 says what that
  means: what is in scope is the `format::flatbuffers::gen` module section 4.8
  describes — its items, their signatures and their values — and any module that
  exports them satisfies the contract, however you produced it.
- No in-scope check uses the network, a cloud backend, MinIO or Azurite. Do not
  make an offline code path depend on one.
- Local checks before you finish: `cargo build` in `/app` succeeds,
  `grep '^name' /app/icechunk/Cargo.toml` prints `name = "icechunk"`, and
  `Cargo.lock` is consistent with the manifests. Leave the workspace so a fresh
  session can run the checkpoint commands in `/app/WORKSPACE_HANDOFF.md`
  unchanged.
