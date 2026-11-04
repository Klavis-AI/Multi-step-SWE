# Workspace handoff — milestone 09

## What this workspace is

`/app` is one cumulative Rust workspace carried forward across every milestone
of this task. It is the `icechunk` transactional array store: a library crate at
`/app/icechunk`, a command-line binary behind that crate's `cli` feature, and an
on-disk format. Milestones 01 to 08 are already implemented here; this milestone
adds the second on-disk generation — a single mutable repository table of
contents — and the API changes that come with it.

What `/app` looks like **when you arrive** (three workspace members, inherited
from milestone 08):

```
/app
├── Cargo.toml              workspace root; default member `icechunk`
├── Cargo.lock              committed, must stay resolvable
├── icechunk/               the library crate and the `icechunk` binary
├── icechunk-python/        the PyO3 binding crate (not scored; see below)
├── icechunk-macros/        the in-repo proc-macro crate
├── TEST_INTERFACE.md       the public contract this milestone is held to
├── WORKSPACE_HANDOFF.md    this file
├── icechunk-spec/          the pinned specification corpus (read-only)
└── timer.sh                how much of the budget is left
```

What it must look like **when you hand off**. This is the one milestone in the
ladder that re-shapes the workspace: the library is split into six crates so the
core can be built without async, without a filesystem and without the cloud SDKs,
and `icechunk` re-exports them so that almost every path a caller used before
still resolves.

```
/app
├── Cargo.toml                    nine members, default member still `icechunk`
├── Cargo.lock
├── icechunk/                     the façade library crate and the binary
├── icechunk-types/               ICError, Path, ETag, the sealed trait
├── icechunk-format/              the on-disk format, re-exported as `icechunk::format`
├── icechunk-storage/             the Storage trait, Settings, S3 configuration types
├── icechunk-s3/                  the AWS-SDK backend
├── icechunk-arrow-object-store/  the object_store backends
├── icechunk-python/              unchanged in role
├── icechunk-macros/              unchanged in role
├── icechunk-js/                  new; `publish = false`, not scored (see below)
├── TEST_INTERFACE.md
├── WORKSPACE_HANDOFF.md
├── icechunk-spec/
└── timer.sh
```

`default-members = ["icechunk"]` does **not** change, so a bare `cargo build` at
`/app` still builds only `icechunk`.

## Preserve what is already here

Everything under `/app` is carried into the next milestone, and every earlier
milestone's behaviour is re-scored by this milestone's verifier.

* **This release removes and renames a great deal.** Unlike milestone 08, it is
  not additive: `Session::{flush, rewrite_manifests, commit_rebasing, changes}`
  are gone, `Session::commit` is no longer `async`, `ChangeSet` became an enum,
  the blanket `From` impls that made `?` convert between error kinds were
  deleted, and several modules moved out of `icechunk::storage` and
  `icechunk::format`. `TEST_INTERFACE.md` §19 is the complete list. Follow it;
  do not invent your own renames on top of it.
* **Keep every path `TEST_INTERFACE.md` §2.2 names resolving.** The verifier
  compiles a driver crate *outside* `/app` that depends on `icechunk` by path and
  uses only `icechunk::` paths. A symbol that moves to a sibling crate without a
  re-export in `icechunk` is, from the verifier's side, a symbol that was
  deleted.
* Do not change the crate's identity: the package stays `icechunk` at
  `/app/icechunk`, and its version is **`2.0.0`** at this milestone (either
  literally or through `version.workspace = true`).
* Do not re-pin a dependency that crosses the public API (`bytes`, `chrono`,
  `futures`, `serde`, `serde_json`, `thiserror`, `tokio`, `url`, `flatbuffers`,
  `zstd`), and let cargo — not you — maintain `/app/Cargo.lock`, leaving it a
  regular committed file consistent with the manifests. The verifier compiles
  its driver crate `--locked`, preferring a lockfile the benchmark resolved from
  this release's upstream manifests; where that one does not fit your manifests
  it falls back to seeding from yours, and then to re-resolving both together.
  Those fallbacks are supported and cost nothing by themselves — see
  `TEST_INTERFACE.md` §1.4 — but they are a rescue path inside a bounded
  resolution budget, not a guarantee. Adding a new dependency is fine. What does
  cost every scored group at once is a manifest or lockfile no resolution can
  satisfy.
* Keep the library building with default features, and keep the binary building
  with `--features cli`. The verifier runs exactly:

  ```bash
  cd /app && cargo build
  cd /app && cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk
  ```

* Keep `icechunk::cli` present as a module even when the `cli` feature is off,
  and keep the command grammar of `TEST_INTERFACE.md` §18.4 working unchanged.
* A repository written by milestone 08 is a **first-generation** repository. It
  must keep opening, reading *and* writing here; only the newly written default
  changes. `TEST_INTERFACE.md` §16.

## Before you hand off

Run, and leave passing:

```bash
cd /app
cargo build                                                          # the library
cargo build --manifest-path /app/icechunk/Cargo.toml \
            --features cli --bin icechunk                            # the binary
cargo test  -p icechunk --lib                                        # the crate's own tests
```

The first two are the verifier's own build steps (`TEST_INTERFACE.md` §1.1) and
a failure in either scores zero. The third is for the milestones that follow
you: your own tests are never scored (`TEST_INTERFACE.md` §20).

Note that `icechunk/Cargo.toml` sets `autotests = false` at this release, so a
new file under `icechunk/tests/` is silently not compiled unless you add it as a
`mod` in `tests/main.rs` or give it its own `[[test]]` entry.

Then exercise this milestone's own path at least once by hand, over a temporary
directory:

1. `Repository::create(None, storage, HashMap::new(), None, true)` and check the
   directory holds `repo` and no `refs/` and no `config.yaml`;
2. commit, branch, tag, and read the table of contents back with
   `repo.asset_manager().fetch_repo_info()`;
3. read `repo.ops_log()` and see the commit, the branch and the tag in it,
   newest first;
4. `session.commit("m").amend().execute()`, `…​.anonymous().execute()`, a
   `session.fork()` merged back into its base, and a `move_node` in a
   `rearrange_session`;
5. migrate a first-generation repository with
   `migrations::migrate_1_to_2(repo, true, true, None)` and then with
   `dry_run = false`, and check the ancestry is unchanged.

Leave `/app` reproducible: no stray build output committed into source
directories, no scratch repositories left under the workspace, no edits to
`icechunk-spec/`.

## What is out of bounds

Only `/app` is carried between milestones. The verifier's own files under
`/tests`, later steps' directories, solution payloads and expected answers are
not present and must not be sought out, inspected or copied.

`/app/icechunk-spec/` is a read-only corpus: read it, never edit it. Start with
its `README.md`, then `PROFILE.md`.

The PyO3 crate `icechunk-python` is part of the workspace but is not scored, and
neither is anything that exists only inside it. The new `icechunk-js` crate is
likewise **not scored as a working package**: no test runs Node, `wasm-pack` or a
browser. What *is* scored is the core refactoring that makes such a binding
possible — the workspace split above, the re-export façades, and the
`compat::IcechunkBoxFuture` shim in the chunk-reader and chunk-writer
signatures. `TEST_INTERFACE.md` §20 lists exactly what is out of scope and why.
