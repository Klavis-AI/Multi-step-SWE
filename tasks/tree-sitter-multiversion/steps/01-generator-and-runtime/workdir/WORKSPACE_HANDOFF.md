# Workspace handoff — milestone 01

This file describes the *workspace*: what `/app` holds, what has to survive to
the next session, and how someone restoring a checkpoint picks the work up. It
does not describe the product — the product is defined by `/app/spec/` and
`/app/TEST_INTERFACE.md`, and where this file and those disagree, they win.

## What `/app` contains right now

- `spec/` — the specification you implement against. Author-owned, replaced at
  the start of every milestone, and **never edit it**. It grows: each milestone
  adds its own document beside the ones already there, so the copy in front of
  you is the whole contract to date.
- `TEST_INTERFACE.md` — the contract the hidden verifier builds and invokes
  against. Also refreshed every milestone; read it again even if you read the
  previous copy, because what is observed can change.
- `WORKSPACE_HANDOFF.md` — this file.
- `timer.sh` — an advisory countdown for this milestone's budget. Nothing reads
  its output; it is for you.

There is no source tree yet. Everything under `/app` besides the four items
above is yours to create, starting from nothing.

## Preservation rules

1. `/app/build.sh` must exist, be executable, and produce an executable
   `/app/bin/tree-sitter`. That is the only entry point the verifier has. It is
   run unprivileged, with a warm crate cache, and it must not need the network.
2. Do not delete or edit `spec/`, `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md`
   or `timer.sh`. The next milestone's overlay replaces them; edits are lost,
   and a deletion can strand the next session without its contract.
3. Everything else under `/app` is yours. Layout, crate names, module structure
   and dependency choices are all your call except where `TEST_INTERFACE.md` or
   `spec/` pins them — the verifier only ever invokes the built binary at its
   documented process boundary.
4. Leave the tree in a state that builds. The next session starts from exactly
   what you leave, with no chance to ask you what you meant.

## The gate

This milestone's verifier carries a hard gate: proof that the binary builds and
runs at its documented process boundary. The gate earns nothing — it can only
zero the milestone.

## Handing off

Whatever notes you want the next session to have, put them in a file of your
own choosing under `/app` — a `NOTES.md`, comments, anything. It will be
carried forward verbatim. Do not put them in this file; it is overwritten.
