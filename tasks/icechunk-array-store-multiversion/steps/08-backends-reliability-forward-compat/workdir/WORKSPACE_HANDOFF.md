# Workspace handoff — milestone 08

## What this workspace is

`/app` is one cumulative Rust workspace carried forward across every milestone
of this task. It is the `icechunk` transactional array store: a library crate at
`/app/icechunk`, a command-line binary behind that crate's `cli` feature, and an
on-disk format. Milestones 01 to 07 are already implemented here; this milestone
adds backends, reliability and forward compatibility on top of them.

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

## Preserve what is already here

Everything under `/app` is carried into the next milestone, and every earlier
milestone's behaviour is re-scored by this milestone's verifier. So:

* Do not delete or rename an existing public module, type, function or CLI
  subcommand. This release **removes and renames nothing** — see
  `TEST_INTERFACE.md` §13.
* Do not change the crate's identity: the package stays `icechunk` at
  `/app/icechunk`. Its version **at this milestone is `0.3.24`** — the workspace
  reaches you carrying the previous milestone's `0.3.4`, so bumping it is part
  of the release rather than something already done for you. Nothing is scored
  on the version string itself; the drivers depend on the crate by path.
* Nothing this milestone is scored on requires adding, removing or re-pinning a
  dependency of `icechunk`, so leave the manifests as you found them. Let cargo
  maintain `/app/Cargo.lock` rather than editing it by hand, and leave it a
  regular committed file consistent with the manifests: the verifier builds its
  own driver crate `--locked`, and where its own task-owned lockfile does not
  fit your manifests it falls back to resolving from yours — an ordinary,
  supported path, not a penalty. `TEST_INTERFACE.md` §1 states the resolution
  order in full. What does cost every scored group at once is a manifest or
  lockfile no resolution can satisfy.
* Keep the library compiling and usable with **no features enabled**. The
  verifier builds `cargo build` at `/app` first, and only then
  `cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk`.
* Keep `icechunk::cli` present as a module even when the `cli` feature is off.

## Before you hand off

Run, and leave passing:

```bash
cd /app
cargo build --manifest-path /app/Cargo.toml                    # the library, no features
cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk
cargo test  -p icechunk --lib                                 # the crate's own tests
```

Then exercise the milestone's own path at least once by hand — create a
repository over a temporary directory, write a native, an inline and a virtual
chunk, commit, read the three back through `Session::get_chunk_ref`, and check
that putting an object at the key `repo` under the repository prefix makes
`Repository::open` fail with `RepositoryErrorKind::RepositoryIsV2`.

Leave `/app` reproducible: no stray build output committed into source
directories, no scratch repositories left under the workspace, no edits to
`icechunk-spec/`.

## What is out of bounds

Only `/app` is carried between milestones. The verifier's own files under
`/tests`, later steps' directories, solution payloads and expected answers are
not present and must not be sought out, inspected or copied.

`/app/icechunk-spec/` is a read-only corpus: read it, never edit it.

The PyO3 crate `icechunk-python` is part of the workspace but is not scored, and
neither is anything that exists only inside it. `TEST_INTERFACE.md` §14 lists
exactly what is out of scope and why.
