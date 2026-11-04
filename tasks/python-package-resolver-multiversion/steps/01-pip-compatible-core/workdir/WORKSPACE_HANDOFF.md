# WORKSPACE_HANDOFF.md

This file is the checkpoint record for `/app`. It exists so that a fresh
session, starting with no memory of this one, can pick the workspace up, know
what is already true of it, and continue without re-deriving anything.

It is a description of the workspace, not a specification of the product.
Where it conflicts with `/app/SPECS/`, `/app/PROFILE.md` or
`/app/TEST_INTERFACE.md`, those win.

## What is in `/app`

`/app` is the `uv` Cargo workspace as this step starts, and no milestone is
implemented in it. The root manifest, the member crates under `crates/`, the
workspace lockfile at `/app/Cargo.lock` and the binary target `uv` in the `uv`
crate are all laid out, but the source is a skeleton the milestone builds out:
`crates/uv/src/main.rs` is an empty `fn main`, and every library crate's
`src/lib.rs` is an empty file. `cargo build --bin uv` from `/app/Cargo.toml`
is the build; it is expected to succeed from a cold `target/`, and the binary
it produces at the outset does nothing. Everything `/app/PROFILE.md` § 1
describes is yours to write.

The documents that define the task sit alongside the source and are not part of
the product:

- `/app/SPECS/specs/` — the published specifications that govern behaviour.
  `/app/SPECS/MAP.md` indexes them and says which milestone each one enters at.
- `/app/SPECS/changelog/step-01.md` — the upstream release notes for the span
  this checkpoint sits inside, newest first. Prose, not contract; useful for
  understanding *why* a behaviour changed, never for deciding what it is.
- `/app/PROFILE.md` — the normative behavioural profile: command surface,
  flag grammar, defaults, diagnostics, exit statuses, and the choices the
  specifications leave open.
- `/app/TEST_INTERFACE.md` — the contract the checkpoint is read through.
- `/app/timer.sh` — the budget clock.

## Checkpoint commands

Run these before considering the milestone done. They are the local checks the
workflow expects; the checkpoint runs its own, from a cold target directory.

    cd /app
    cargo build --bin uv                       # must succeed
    cargo test --workspace                     # the inherited tests, if any
    ./target/debug/uv --version                # the binary runs

The candidates for `cargo build` are cached in a `CARGO_HOME` the checkpoint
provides. Build with `CARGO_NET_OFFLINE=true` to see the same view it will:
every crate the lockfiles the checkpoint prewarms name is present, and a
crate none of them names is not.

## Preservation rules

- The workspace stays cumulative. Source, manifests, the lockfile and the
  documents above are inherited; do not delete, replace, move or rewrite a
  document under `/app/SPECS/` or the four top-level documents.
- `/app/SPECS/` is verbatim upstream text under the licences reproduced in
  `/app/SPECS/licences/`. Never edit, trim, paraphrase or annotate it. If a
  requirement is missing from the documents, the place to write it is
  `/app/TEST_INTERFACE.md`.
- Add source rather than reshaping it. A milestone that needs a new crate or
  module adds one; it does not renumber or rename the inherited ones.
- No dependency outside the lockfiles the checkpoint prewarms can be added:
  the checkpoint builds offline against a registry holding their union.

## Handing off

When the milestone is complete, leave `/app` in a state where the next session
can start immediately:

1. `cargo build --bin uv` succeeds from a cold target directory.
2. `cargo test --workspace` passes, or the failures are written down here with
   the reason.
3. This file still describes the workspace accurately. If the milestone added a
   crate, changed the entry point, or altered a documented command, update the
   description above before finishing — a handoff that has drifted is worse
   than none.
