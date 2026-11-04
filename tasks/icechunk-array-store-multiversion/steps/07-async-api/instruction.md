# 07 — The asynchronous API

## Implement this milestone

Continue the cumulative Icechunk implementation in `/app`, taking the crate from
`0.3.0` to **`0.3.4`**.

This milestone is the **asynchronous API** release. It has two halves, and they
live in different places:

* In **`icechunk-python`**, every method that does I/O on `Repository`,
  `Session` and `Store` gains an asynchronous twin named with an `_async`
  postfix, beside the existing synchronous one. Both call the same Rust future
  on the same runtime, so the two interfaces may be used on the same instance
  and a synchronous and an asynchronous run over one repository produce
  identical object stores. Only methods that do I/O get a twin.
* In the **Rust crate**, which is already fully asynchronous, the release lands
  a different set of changes: a virtual chunk container's `url_prefix` must now
  end in `/`, and prefix matching is exact — so authorising `s3://foo` no longer
  authorises `s3://foo-production`, while a persisted configuration or an
  authorisation map that predates the rule is normalised rather than broken; a
  conflict solver reference widens to `Send + Sync`; `VirtualChunkLocation`
  becomes opaque behind `url()`; `CannotParseUrl` becomes a struct variant
  carrying its URL; garbage collection and chunk accounting gain mandatory
  memory and concurrency parameters and a dry-run mode; snapshot traversal is
  renamed and re-typed; the default asset caches triple; and an oversized
  manifest or snapshot draws a log warning naming the configuration field that
  would fix it.

**The Rust crate is the contract.** The Rust-observable half above is required,
and each of the six milestones you inherit must still work end to end. The
Python asynchronous twin, the `ManifestFileInfo` property accessors, the
default-on container validation, and the cache warning's log text sit outside
that contract; `/app/TEST_INTERFACE.md` §5 says so explicitly and says why.
Build the Python half because the milestone calls for it.

Everything the earlier milestones established must keep working: the repository
format and reference semantics, sessions and conflict rebase, expiration and
garbage collection, the FlatBuffers metadata format, the `icechunk` command-line
interface, and manifest splitting with its fixed first-snapshot id.

## Read these local references

Normative for this milestone:

* `/app/TEST_INTERFACE.md` — the interface contract: §1 what to add, §2 what to
  preserve (including §2.7, the inherited signatures that must stay callable by
  name, with their argument and return types), §3 the thirty-two migration
  items — twenty-one changed, six new, one internal, four pinned as unchanged —
  §4 the environment your workspace is built and run in and how it is built,
  and §5 what is out of scope.
* `/app/icechunk-spec/README.md` — a map of the pinned documentation corpus.
* `/app/icechunk-spec/PROFILE.md` — how the corpus and this milestone's contract
  line up, which documents carry this release's new statements, and (§5) which
  documents in the tree are superseded leftovers you should not read.
* `/app/icechunk-spec/guide/async.md`, `guide/howto.md`, `guide/configuration.md`,
  `guide/faq.md` — this release's changed and new prose.
* `/app/icechunk-spec/spec.md` and `guide/performance.md` — unchanged at this
  release; no new requirements come from them.

Also read:

* `/app/WORKSPACE_HANDOFF.md` — what is already in the workspace, what you may
  not break, and what you hand to the next milestone.

## Meta requirements

* Your budget for this step is **7,200 seconds**. Run `bash /app/timer.sh` to
  see what is left. The configured timeout is authoritative.
* `/app` is cumulative and is handed to the next milestone. Do not delete or
  rewrite earlier milestones' work; migrate call sites instead. Do not modify
  anything under `/app/icechunk-spec/`.
* `cargo build --locked` at `/app` must succeed, and so must
  `cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk`.
  Keep `Cargo.lock` consistent with the manifests.
* This environment has no network access, and everything the build
  needs is already installed. The pinned dependency set in
  `/app/Cargo.lock` is pre-cached in the image and is the whole
  resolvable crate graph; do not add dependencies. Searching for, downloading, copying, wrapping or invoking
  an online implementation of this task, or any task-specific answer, is
  prohibited — as is inspecting `/tests`, later steps, or any solution material.
* Your workspace must work in a separate container with no network, no cloud
  endpoint and no object-storage service. Anything observable must work with
  in-memory or local-filesystem storage and `file://` virtual chunk references.
* Check your own work before you finish: build the library and the binary, run
  the crate's own tests, and exercise the new behaviour yourself.
