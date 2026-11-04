# Milestone 06 — manifest splitting and format 1.0

## 1. Implement this milestone

`/app` already holds the Icechunk workspace as the previous five milestones
left it. Extend it, in place, so that this release's capability works:

* **Split an array's chunk references across many manifests.** A repository
  configuration can carry a splitting policy: rules that select arrays, and,
  per selected array, per-axis split sizes that cut the chunk grid into cells.
  One manifest is written per non-empty cell. Reads are completely unaffected
  by the policy — the same committed data must read back byte-identically under
  any policy, including none.
* **Re-lay-out an existing repository.** An operation rewrites every chunk
  reference under the currently active policy and records that policy in the
  new commit's metadata.
* **Select which manifests are preloaded** when a session opens, through a
  predicate over an array's path or name and the manifest's reference count.
* **Give every repository the same, well-known first snapshot**, so that a
  single lookup identifies a prefix as an Icechunk repository.
* **Commit with rebase in one call**, retrying a bounded number of times.
* **Make object-store retries configurable** — attempts and backoff bounds.
* **Require virtual chunk containers to be declared and authorised
  explicitly**, by url prefix. There are no built-in containers any more, and a
  prefix that was not authorised is refused before any I/O.

Everything the earlier milestones established must keep working: the on-disk
layout and the two kinds of reference; sessions, commit conflicts and rebase;
snapshot expiration and garbage collection; the binary metadata format and its
fixed header; and the `icechunk` command-line interface. Several inherited
signatures change at this release — the migration list in
`/app/TEST_INTERFACE.md` §5 is exhaustive, and anything not on it must behave
as it did before.

The on-disk format is **frozen** at this release. That is a compatibility
promise, not a change: do not bump the spec-version byte.

## 2. Read these local references

Normative specification documents, copied verbatim, under
`/app/icechunk-spec/`:

* `README.md` — the index of the corpus: what is in it, which files earlier
  milestones left behind, and the order to read the rest in. It is a map, not a
  document to work from, and it sends you to `PROFILE.md` first.
* `PROFILE.md` — **read this first**, before any copied document: every place
  where those documents and the required behaviour disagree, and which one
  wins. It also lists the documented Python spellings that have a different
  Rust spelling, or none.
* `guide/performance.md` — manifest splitting and preloading. §"Splitting
  behaviour" is the one to read twice: it is where the *incremental* rule
  lives, that a commit keeps the manifests whose bounding boxes the active
  grid still contains and rewrites only what it must.
* `guide/migration.md` — the breaking changes at this release.
* `guide/configuration.md` — `RepositoryConfig` field by field.
* `spec.md` — the on-disk format and the repository algorithms.
* `guide/{virtual,version-control,expiration,storage,faq}.md` and
  `flatbuffers/*.fbs` — the inherited behaviour this milestone must preserve.

Everything under `/app/icechunk-spec/` is read-only. Do not edit, trim or
annotate a copied document.

Additional files, both at `/app`:

* **`/app/TEST_INTERFACE.md`** — the binding public contract: module paths,
  exact signatures, argument order, defaults, error variants, file layout,
  build commands, the migration list, and §7 "what is out of scope".
* **`/app/WORKSPACE_HANDOFF.md`** — what `/app` contains, what you must
  preserve, how to checkpoint, and how a fresh session resumes this work.

## 3. Meta requirements

* **Budget: 7,200 seconds.** Run `bash /app/timer.sh` to see elapsed and
  remaining time. The configured timeout is authoritative; the script is
  an estimate.
* **The workspace is cumulative.** Work inside `/app` and preserve everything
  earlier milestones built. `cargo build` at `/app`, and
  `cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk`,
  must both succeed when you finish. Keep `/app/Cargo.lock` in step with the
  manifests. Leave the tree clean; `target/` is not collected.
* **Network.** This environment has no network access, and everything the build
  needs is already installed. The pinned dependency set in
  `/app/Cargo.lock` is pre-cached in the image and is the whole
  resolvable crate graph; do not add dependencies. Searching for, downloading, copying, vendoring,
  wrapping or invoking an existing implementation of this project — or any
  task-specific answer — is prohibited.
* **Workspace scope.** `/app` as you leave it when you finish is the
  deliverable. Do not read or modify anything under `/tests`, later milestones'
  material, or solution material. Your own tests are yours: run them freely,
  but they are not part of this milestone's requirements, so do not write code
  that only satisfies them.
* **Handoff.** Only `/app` travels. Anything a successor session must know that
  is not obvious from the code goes in `/app/NOTES.md` or in comments beside
  the code — never in `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md` or
  `icechunk-spec/`, which the next milestone's overlay replaces.
