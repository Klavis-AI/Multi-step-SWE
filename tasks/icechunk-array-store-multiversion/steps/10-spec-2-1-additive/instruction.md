# Step 10 — Spec 2.1

## 1. Implement this milestone

Advance the Icechunk workspace in `/app` to **on-disk spec version 2.1**, an
additive, forward- and backward-compatible change to spec 2.0, and to the
library capabilities that ship with it:

- **One new format field.** `SnapshotInfo` in the repo flatbuffer gains an
  optional `pruned_ancestor_tx_logs: [ObjectId12]`. Expiration populates it with
  the transaction-log ids of the ancestor commits it removed, oldest first. No
  other table or field changes, there is **no new spec-version constant**, and
  the header byte stays `2` for both spec 2.0 and 2.1.
- **Expiration stays correct.** With that ancestry recorded, `diff`, `amend`,
  `rebase` and `inspect` produce complete results across an expiration boundary,
  and garbage collection retains every transaction log the field references —
  and records the same ancestry itself when it collects a snapshot that a
  surviving one still parents onto (`TEST_INTERFACE.md` §2.3, E9). A referenced
  log that has gone missing degrades predictably: `diff` and `inspect` skip it,
  `rebase` errors.
- **Header inspection.** A read-only API parses a metadata file's binary header
  from the first bytes of the object, without downloading or decoding the body,
  and asset-manager wrappers expose it per file kind.
- **Static HTTP headers.** The HTTP object-store configuration carries a header
  map that is injected into every request, alongside the existing transport
  options, without either clobbering the other.
- **Explicit no-credential sentinels** for virtual chunk containers that need no
  credentials, with the old implicit `None` deprecated but still accepted.
- **Empty-prefix repositories.** Creating a repository at an object store's
  bucket root is refused before any I/O; existing ones still open and update.
- **Virtual chunk locations** preserve every part of their URL — userinfo, port,
  query, fragment and empty path segments — while `file://` locations stay
  normalized, and the port is honoured wherever a location is matched or
  resolved.

Everything the workspace could already do must keep working: the spec-1 and
spec-2 repository formats, sessions and conflict rebase, expiration and garbage
collection, the FlatBuffers format, the command-line interface, manifest
splitting, the asynchronous API, the storage backends, and the Icechunk 2 table
of contents with its operations log. All of them remain required.

## 2. Read these local references

Normative specification documents, all under `/app/icechunk-spec/`:

- `spec.md` — the current specification, **version 2.1**. Start at
  `## Changes from spec version 2 to 2.1`, then `##### \`SnapshotInfo\`` and
  `### Binary File Format`.
- `reference/version-policy.md` — `## On-Disk Format` and `### Compatibility`:
  what a minor spec version means and what the header byte encodes.
- `reference/spec-v2.md` and `reference/spec-v1.md` — the frozen earlier
  generations, for comparison.
- `flatbuffers/*.fbs` — the normative schemas; `repo.fbs` carries the one new
  field.
- `design/016-expired-transaction-logs.md` — the expiration design, including
  the GC retention rule and the diff / amend / rebase / inspect consequences.
- `guide/guides-storage.md` (`### Empty prefix and legacy repositories` and
  `### HTTP Storage`, which says the HTTP backend is read-only),
  `guide/guides-configuration.md` (`## Virtual Chunk Credentials`),
  `guide/guides-virtual.md`, `guide/understanding-expiration.md`,
  `guide/guides-observability.md`, and `reference/changelog.md`.
- `README.md` for the map of the corpus, `PROFILE.md` for the resolutions this
  task has already fixed — read both before the rest. The corpus is **overlaid**
  by each milestone rather than replaced, so 35 files in it were copied at an
  earlier release; `README.md` tabulates them and `PROFILE.md` R12 declares them
  non-normative. `reference/spec.md` is one of them: it is spec **2.0**, not
  2.1. Do not edit anything under `/app/icechunk-spec/`.

Additional files, outside the corpus:

- **`/app/TEST_INTERFACE.md`** — the public contract this milestone is held
  to: exact paths, signatures, error categories, the CLI contract, the
  migration list, and an explicit section on what is *out of scope*. Read it
  before writing code.
- **`/app/WORKSPACE_HANDOFF.md`** — what `/app` is, what is preserved between
  sessions, and how to checkpoint.

## 3. Meta requirements

- **Budget: 7,200 s.** Run `bash /app/timer.sh` to see elapsed and remaining
  time. The configured timeout is authoritative if the two ever disagree.
- `/app` is one **cumulative** workspace carried forward from the previous
  milestone and handed to the next. Do not delete or truncate inherited source,
  keep `cargo build` at `/app` green, and keep
  `cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk`
  green. Build output under `target/` is not collected.
- **Network rule.** This environment has no network access, and everything the build
  needs is already installed. The pinned dependency set in
  `/app/Cargo.lock` is pre-cached in the image and is the whole
  resolvable crate graph; do not add dependencies. Searching for, downloading, copying, wrapping
  or invoking an online implementation or a task-specific answer is prohibited.
- **Workspace scope.** `/tests`, later steps and any solution material are
  off limits: do not read them, look for them, or write anything that depends on
  them. Your crate must work when used from a separate crate outside `/app`
  through the public interface in `/app/TEST_INTERFACE.md`; your own tests are
  out of scope and are never run.
- Check your work locally with `cargo build`, `cargo test --lib`, and any
  targeted integration test that does not need a cloud endpoint. Leave the
  workspace in the state described in `/app/WORKSPACE_HANDOFF.md`.
