# `environment/assets/prefetch/` — the crate registry the agent image ships

The agent phase runs against a network allowlist: the model's inference
endpoint is the only reachable host. Nothing else — not crates.io, not GitHub,
not a docs mirror — can be reached, so every crate the candidate's
`cargo build --bin uv` may legitimately ask for has to be in the image before
the agent starts. That is what these assets are for.

## Layout

    prefetch/NN/<tag>/Cargo.toml            the lockfile's external packages, exact-pinned
    prefetch/NN/<tag>/Cargo.lock            the same set as a lock, as a version hint
    prefetch/NN/<tag>/rust-toolchain.toml   the channel that release declares
    prefetch/NN/<tag>/stubs/<name>/         empty stand-ins for patched names (see below)
    prefetch/NN/<tag>/src/lib.rs            stub source for the stand-in package

`NN` is the step, `<tag>` is one of the trees that step's checkpoint prewarms
(`starter`, `0.1.0`, …). The Dockerfile copies the tree to `/opt/prefetch`,
runs `assets/prefetch/fetch.sh` over it, deletes the tree, and widens the
permissions on `CARGO_HOME`. What ships in the image is the registry cache and
the git checkouts — no manifest and no source of the workspace.

`fetch.sh` fetches each stand-in with the image's default toolchain and, when
the release's declared channel is installed too, with that channel as well:
`CARGO_HOME`'s registry and git directories are keyed by a source-id hash that
changed between cargo releases (1.98.1 reads
`registry/cache/index.crates.io-1949cf8c6b5b557f/`, 1.75 reads
`index.crates.io-6f17d22bba15001f/`), so one toolchain's cache is invisible to
the other.

## Why one stand-in per *lockfile*, not per step

A step's dependency set is the union of the lockfiles it spans, but a union of
lockfiles is not itself a resolution cargo will accept: two of them can name
two versions of one crate that cargo's semver rules require it to unify
(`anstream 0.6.11` in the starter tree, `0.6.13` one release later), and one
stand-in naming both cannot resolve. Within a single lockfile no such pair
exists — cargo would already have unified it — so exact pins are always
satisfiable per directory, while the shared `CARGO_HOME` still accumulates the
union.

## Why `default-features = false` on every entry, and why every entry is a root

Every package the lockfile names is declared, so nothing the build can need is
missing. Turning default features off keeps the resolver from additionally
walking optional dependencies that *no* lockfile names — the fetched set is
then the lockfiles' package set exactly, which is what the checkpoint's own
registry holds (`WORKSPACE_HANDOFF.md`: "every crate the lockfiles the
checkpoint prewarms name is present, and a crate none of them names is not").

## Regenerating

    python3 environment/assets/prefetch/generate.py          # rewrite
    python3 environment/assets/prefetch/generate.py --check  # verify up to date

Both read only `steps/NN/tests/skeleton/*/Cargo.lock` — the same lockfiles the
steps' checkpoints build their own registries from — so the two registries
cannot drift apart.

## What is deliberately *not* here

No workspace crate source: the crates under `crates/` (`uv`, `uv-resolver`,
`pep440-rs`, …) are the task, and no stand-in fetches, builds or ships them.

Two *registry* packages do collide with the task's own crate names, and they
are present because the build cannot proceed without them:

* `pep440_rs 0.3.12` and `pep508_rs 0.2.3`

Both are astral's published crates — the upstream implementations of parsers
this task asks the candidate to write — and both are reached through
`pyproject-toml 0.8.1`, a **direct, non-optional** dependency of the `uv` crate
in the `starter` and `0.1.0` manifests (`uv → pyproject-toml 0.8.1 →
pep440_rs 0.3.12`, and in the starter tree also `pep508_rs 0.2.3`).  The
workspace's own `[patch.crates-io]` replaces these names with its `crates/`
copies, but the patch only applies when the path crate satisfies the
requirement — at `starter`/`0.1.0` the path crates are `0.4.0`/`0.2.3`, and
`pyproject-toml 0.8.1` wants `^0.3.6`/`^0.2.1`, so `pep440_rs 0.3.12` (and, in
the starter tree, `pep508_rs 0.2.3`) really is resolved from the registry, and
really is compiled into the checkpoint's build.  `cargo fetch` on those
lockfiles fails without them and `cargo build --offline` cannot compile
`pyproject-toml` without their sources, so leaving them out would break the
offline build at every step whose candidate still holds the seed lockfile —
which the documents order it to preserve.

Everywhere the workspace's patch *does* apply — tag `0.1.0`'s `pep508_rs`, and
`0.1.24`'s `pyproject-toml 0.10.0`, which wants `pep440_rs ^0.5.0` and
`pep508_rs ^0.4.2` — the stand-in carries the same patch, pointed at the empty
stubs under `stubs/<name>/` (see below).  Without it, cargo resolves those
requirements from crates.io and the image would hold two more copies of the
parsers the task is about (`pep440_rs 0.5.0`, `pep508_rs 0.4.2`) that no
lockfile names and the checkpoint's registry does not hold.

These two remain baked in, with the conflict recorded rather than resolved;
removing them needs a change on the task side (drop `pyproject-toml` from the
seed manifests, or move to a release whose pep440/pep508 dependency the path
crates satisfy), which this round is not permitted to make.

## The `stubs/` directories

`stubs/<name>/` is an empty package (`[lib] path = "src/lib.rs"`, no code) that
stands in for one of the workspace's own crates, so that a `[patch.crates-io]`
entry has something to point at.  The stand-in's `Cargo.toml` patches exactly
the names that (a) appear in the lockfile as a path member, (b) are depended on
by name by some package in the lockfile, and (c) have no registry entry in that
same lockfile — the shape the workspace's own patch produces.  The stub takes
the path member's version and the workspace crate's feature names, so it
satisfies the same requirements the workspace's patch satisfies; it is never
built, and it holds nothing from upstream.

