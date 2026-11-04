# Workspace handoff — step 5

## Where things are

This checkpoint keeps its crates under **`/app/crates/`**: a crate named
`tor-cell` lives at `/app/crates/tor-cell`. The grader builds against that
layout. Moving the crates would break it.

| Path | What it is |
| --- | --- |
| `/app/Cargo.toml` | Workspace root manifest. Add new crates to `members`. |
| `/app/Cargo.lock` | Your resolved dependency graph. **Keep it committed.** |
| `/app/Cargo.lock.release` | This release's own pinned graph — the graph this step's milestone is defined against, and what the reference implementation's workspace resolves to. **Adopt it.** |
| `/app/crates/` | The crates. |
| `/app/SPEC_NOTES.md` | Normative protocol facts for this step, with citations. |
| `/app/TEST_INTERFACE.md` | Every public item this step is graded through. |
| `/app/WORKSPACE_HANDOFF.md` | This file. |
| `/app/docs/step-NN/` | Earlier steps' `TEST_INTERFACE.md` and `SPEC_NOTES.md`. |
| `/app/timer.sh` | `bash /app/timer.sh` prints the time left in this step. |

## What you inherited

`/app` carries forward everything the previous eleven-or-fewer steps produced.
The step before this one was asked to deliver preemptive circuits from port prediction and live reconfiguration.

Treat that work as a starting point you are responsible for, not as a finished
foundation you can assume. It may be incomplete, and where it is incomplete
this step's grading still depends on it: behaviour introduced in an earlier
step is re-checked here against the cumulative workspace. If an inherited
subsystem does not work, fixing it is part of this step.

## How to build and check

The pinned toolchain is `rustc 1.98.1`; it is the only toolchain installed.

```sh
cd /app
cargo build --workspace              # everything compiles
cargo test  --workspace              # your own tests, if you write any
cargo clippy --workspace --all-targets
```

Cold workspace builds at this scale take a few minutes; budget for that rather
than discovering it at the end. `cargo check --workspace` is the fast inner
loop.

Four build rules the grader depends on:

- **Keep `Cargo.lock`.** The grader builds from your manifests, and a lockfile
  you deleted cannot be reconstructed identically. A workspace that only
  resolves online is a workspace that may not resolve at grading time.
- **Resolve this release's graph, not merely a graph that works.** The grader
  builds your manifests in the same offline crate set you build in, and it
  compiles your crates against the graph your own `/app/Cargo.lock` records --
  so a green `cargo build --workspace --offline` is the check it claims to be.
  (A lockfile that does not resolve at all falls back to
  `/app/Cargo.lock.release`.)
  What that lock records, though, is a snapshot: it was resolved when this
  workspace was first handed to you, and cargo keeps a locked version while a
  requirement still allows it, so nothing short of re-resolving moves it. The
  ladder's releases do not pin the same versions of the same crates from one
  step to the next, and this step's behaviour is specified against *this
  release's* pins: `/app/Cargo.lock.release`, re-supplied at every step, is the
  graph the reference implementation's workspace resolves to and the one every
  graded case was validated against. Adopt it at the start of each step, before
  you trust a build:

  ```sh
  cd /app
  cp Cargo.lock.release Cargo.lock     # adopt this release's pins
  cargo build --workspace --offline    # ... and make THIS build the green one
  ```

  Cargo keeps every pin it can and re-resolves only what your own tree has
  moved, so the copy is cheap: keep it committed. Where the two graphs
  disagree, the release's is the one this milestone is defined against.
- **Depend only on crates this environment already carries.** The registry is
  replaced by a pinned, pre-fetched set of crates: the union of the dependency
  graphs the releases of this ladder are built from. Adding a dependency
  outside that set fails here, offline, immediately -- `cargo build --workspace
  --offline` is the check -- and grading resolves against that same set, so a
  graph that does not resolve here does not resolve there either.
- **Keep the crate names and paths** that `/app/TEST_INTERFACE.md` uses. The
  grader imports your crates by name from a separate driver crate; a rename is
  indistinguishable from a deletion.

## How you are graded

A separate container receives `/app` — source, manifests and documents, without
`target/` — builds it, and runs a conformance driver that imports your public
items and exercises them. The driver is not in this workspace and you should
not try to reconstruct it.

Grading has two halves, and they **multiply**.

The first is a **gate**. Gate groups carry no weight: passing them cannot raise
your result by a single point. They can only veto — if any one of them fails,
the step scores **zero**, however much of the new work you finished.

What the gate covers: the behaviour signed off at earlier steps. There
is no separate build-proof group at this step -- every gate group here
is inherited regression. A workspace that does not build still fails
them, because a group with a compile error does not pass.

The second is a **score** between 0 and 1 — the weighted fraction of the
remaining groups, which are this step's new capability plus a few smaller
peripheral ones. A group passes only when every case in it passes: no failures,
no ignored cases, no compile error. Credit is per group, not per case, so a
subsystem that is nearly finished counts the same as one never started — finish
whole capabilities in preference to starting many.

Your reward is the gate times the score, so two things follow that are worth
internalising. Breaking something that already worked does not cost you a
fraction of the step; it costs you the whole step. And a workspace that still
builds but implements nothing new earns exactly zero — preserving the past is
necessary, and on its own it is worth nothing.

## Leaving a handoff

A different fresh session continues from your `/app` at the next step. Leave
the workspace building, leave `Cargo.lock` in place, and leave notes for
anything you knowingly left unfinished — put them in a file of your own
choosing under `/app`, not in the documents listed above, which are
verifier-owned and re-supplied every step.
