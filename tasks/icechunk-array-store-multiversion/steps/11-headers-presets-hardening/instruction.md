# Step 11 — headers, presets and hardening (icechunk 2.2.0)

## 1. Implement this milestone

`/app` already holds the Icechunk workspace you delivered for steps 1 to 10: a
Rust workspace of nine crates whose default member is the library `icechunk`,
plus the `icechunk` binary behind the `cli` feature. Continue that
implementation in place. This is the **last** step of the ladder, and it is a
small one: it adds reporting, one storage preset, three command-line
subcommands and four pieces of hardening, and it changes nothing else.

Move the workspace from **2.1.0 to 2.2.0** by adding exactly what
`/app/TEST_INTERFACE.md` §3 and §4 put in scope:

- **`inspect_*` reports each file's header.** All four entry points —
  `snapshot_json`, `repo_info_json`, `manifest_json`, `transaction_log_json` —
  keep their signatures and gain a top-level `header` member naming the library
  version that wrote the file and that file's spec version, file type and
  compression. §3.1 fixes the six JSON member names and their value domains. The
  structs behind that JSON are crate-private, so the **JSON text is the
  contract** — do not make them public to satisfy it.
- **The Hugging Face storage preset** — `new_hf_storage(config, bucket, prefix,
  namespace, credentials, read_headers, write_headers, legacy_rooted_keys)`. It
  sets the endpoint to `https://s3.hf.co/<namespace>`, defaults the region to
  `us-east-1` only when none was given, forces path-style URLs and sends the
  bare bucket name (§3.7).
- **Three new CLI subcommands** — `ancestry`, `branch` (`list`/`create`/`delete`)
  and `tag` (`list`/`create`/`delete`) — and **`snapshot list` is removed**. The
  grammar, the exit statuses and the stdout shape are in §4; the output is plain
  text and never JSON.
- **Deleting a chunk key that cannot exist is a no-op**: coordinates outside the
  grid, a missing node, or a key at a group path. **Writing** such a key is
  still an error (§3.3).
- **A zero-length chunk write is rejected** — inline, virtual and materialized
  alike — with a new `SessionErrorKind::ZeroLengthChunk`. Deleting stays the way
  to record that a chunk is not stored at all (§3.4). `ChunkPayload::length()`
  becomes public (§3.2).
- **The object-metadata keys lose their underscores**: `icspecver`, `icclient`,
  `icfiletype`, `iccompalg`, with the old spellings kept beside them as
  `*_DEPRECATED` constants (§3.5).
- **Garbage collection becomes safe under clock skew**: the retention decision
  uses the object store's `created_at` rather than the host's `flushed_at`, so a
  transaction log a retained snapshot still references is never deleted (§3.6).

**And carry the migration** (§6). Four signature changes ripple through the
workspace, and code written against the documented interface will not compile
against an implementation that skips them:
eight storage constructors gain two `Vec<(String, String)>` header parameters,
`ObjectStoreBackend::mk_object_store` gains a `Role`, `rewrite_manifests`'
concurrency becomes `Option<usize>`, and `object_store` moves 0.13.2 → 0.14.0.
`ManifestConfig` gains the `max_concurrent_manifest_fetches_during_commit` field
and its accessor, which is where `rewrite_manifests(…, None, …)` takes its
concurrency from and which reports **1** by default; and
`CommitBuilder::max_concurrent_nodes(0)` is clamped to 1 rather than silently
dropping every manifest (§6.4, §6.5). The migration is required in its own
right, not only through the code that happens to compile against it.

Two things are **deliberately out of scope**, and both are worth knowing before
you spend budget on them. `PROFILE.md` P24 records the S3 endpoint-with-path
fix, including its seven-case truth table — implement it if you like, but its
function is private and its only caller builds a live AWS client, so it has no
effect either way. And the zero-copy buffer write in this release's changelog is
a Python-layer change; the Rust `Store::set` signature does not move, and no test
can observe it (P26).

Everything steps 1 to 10 put in scope stays in scope and must keep working.
Because this is the last step, the inherited sample is the widest in the ladder:
one end-to-end path through each of the ten earlier milestones is re-exercised
here alongside this step's new capability.

A workspace that does not build satisfies no requirement.

---

## 2. Read these local references

Read these two first. They are the whole contract and they bound everything
else.

1. **`/app/TEST_INTERFACE.md`** — the public contract, self-contained. §1 is the
   workspace and the two build commands; §2 restates the inherited surface **as
   this release spells it**; §3 is this release's new API with exact
   signatures; §4 documents the command line completely — binary name, feature,
   build command, every subcommand and flag, argument grammar, exit statuses,
   stdout format and the configuration file's path and grammar; §5 is the other
   storage presets; §6 is the migration; **§7 says what is out of scope**, and it
   is worth the minute it costs.
2. **`/app/icechunk-spec/PROFILE.md`** — the task-owned resolutions, where the
   specification leaves behaviour open or disagrees with the implementation.
   It is cumulative: sections A and B carry all ten earlier milestones'
   resolutions forward, restated as they hold at **this** release, and section C
   adds this one's (P21–P25). Section D is the standing out-of-scope list. Where
   this file and a specification document disagree, this file wins.

Then the normative upstream documents, in the order they pay off:

| Read | For |
| --- | --- |
| `/app/icechunk-spec/README.md` | how the corpus is laid out and which document is which — including which 35 of its 114 files are superseded copies left behind by earlier milestones |
| `/app/icechunk-spec/reference/changelog.md`, `## Python Icechunk Library 2.2.0` | the normative statement of what is new at this release — one bullet per item in §1 above |
| `/app/icechunk-spec/guide/guides-storage.md`, `#### Hugging Face` | the gateway endpoint, the region, path-style URLs, the bare bucket name, and the lost-response caveat |
| `/app/icechunk-spec/spec.md`, `### Binary File Format` | the 39-byte header whose four fields the new `inspect_*` output reports |
| `/app/icechunk-spec/design/011-IC2-gc-and-expiration-consistency.md` and `016-expired-transaction-logs.md` | the retention rule the clock-skew fix restores, and what `pruned_ancestor_tx_logs` is for |

The rest of the corpus is inherited and unchanged at this release; re-read a
page only where your implementation of that specific area is shaky. Do not
re-read it end to end — the budget is better spent on the code.

Also read **`/app/WORKSPACE_HANDOFF.md`**: how `/app` is collected, what you
must not delete, a checkpoint you can run by hand, and — because this is the
last step — a description of what a complete finished artifact looks like. Run
the checkpoint at the start, before you change anything; four of its lines are
expected to fail on that first run — the four subcommands this step adds — and
it says which.

---

## 3. Meta requirements

- **Budget**: 7,200 seconds. `bash /app/timer.sh` shows the remaining time;
  the configured timeout is authoritative. Spend it on §1's seven items and the
  migration, not on documentation you already have.
- **Cumulative workspace**: work in `/app`. Nothing starts from what you leave,
  but keep the workspace building at every point you might be interrupted — the
  snapshot is taken when time runs out, not when you decide you are finished. Do
  not delete or move `icechunk-spec/`, `TEST_INTERFACE.md`,
  `WORKSPACE_HANDOFF.md` or `timer.sh`. `target/` is not collected; the sources,
  the manifests and `Cargo.lock` are.
- **Network**: This environment has no network access, and everything the build
  needs is already installed. The pinned dependency set in
  `/app/Cargo.lock` is pre-cached in the image and is the whole
  resolvable crate graph; do not add dependencies. Do **not** search for, download, copy, wrap or
  invoke an online implementation or a task-specific answer. This applies with
  full force to Icechunk itself: it is Apache-2.0, its source tree is one clone
  away, and the 2.1.0 → 2.2.0 difference is a published diff. Vendoring,
  transcribing or reconstructing upstream Icechunk source — in whole or in part,
  by any route, including from memory — is prohibited and defeats the point of
  the task. The shipped documentation describes the behaviour to reproduce; the
  implementation must be yours.
- **Off-limits**: do not inspect `/tests`, other step directories, hidden
  test fixtures or solution material, and do not weaken or disable any check.
- **Workspace scope**: `icechunk` must work when used from a separate crate
  **outside** `/app` that declares `icechunk = { path = "/app/icechunk" }`, so
  only public items reachable from `icechunk::` count. Your manifests are used
  as you leave them, and your own tests are not part of the deliverable. Keep
  your test-only dependencies in `[dev-dependencies]`.
- **Handoff**: leave `/app` reproducible — both build commands in
  `TEST_INTERFACE.md` §1 succeed offline from the workspace, `Cargo.lock` is
  present and consistent, and the `WORKSPACE_HANDOFF.md` checkpoint passes in
  full.
