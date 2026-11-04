# WORKSPACE_HANDOFF.md — what you inherit and what you must deliver

## What `/app` is

`/app` is a **go-ethereum working tree**. It is the real upstream project, not a
skeleton: the packages the transition tool depends on — `core`, `core/vm`,
`core/types`, `params`, `crypto`, `trie` — are all present and all compile.

Your job at every step is the job a geth maintainer had at that point in the
project's history: take a tree that implements the ladder up to here and make it
implement one more milestone, without breaking anything below it.

## What you inherit at this step

`/app` is the workspace exactly as the previous step left it: a tree whose
tool already implements **Berlin, London**.

It was built and graded at the end of step 01, so you are starting from a
tree that passes step 01's groups. Keep it that way — this step's inherited
groups re-run those corpora.

## What you must deliver

Implement the two new subcommands described in `/app/TEST_INTERFACE.md`:

- **`t9n`** — validate transactions with no state and no transition: decode
  the envelope, check the signature and intrinsic gas, and report per-
  transaction validity. It must run with no `--input.alloc` and no
  `StateDB` at all.
- **`b11r`** — assemble a block from a header, a transaction list and an
  ommer list, deriving the uncle hash and the block hash. It performs no state
  transition.

`t8n` must keep behaving exactly as it does now at Berlin and London.

`/app/build.sh` must keep working and must keep producing `/app/build/t8n`.

## How you are scored

One score per step, and it is **this step's own score** — not a running total.
It is **`gate × score`**:

- **`gate`** is 0 or 1. A handful of groups are *gate* groups: proof that your
  tool builds and runs, plus regression against the forks you inherited. They
  are worth **nothing**. They only veto. Every case in every gate group must
  pass, or this step scores **0** no matter how much of the new work you did.
- **`score`** is between 0 and 1. It is the weighted fraction of *cases* you
  pass across the **scored** groups — the ones for the capability this step
  adds. Partial credit here is real: half the cases in a group is half that
  group's weight.

This step's **gate** groups (worth nothing, each one a veto):

- `proof-of-life`
- `inherited-london-t8n`
- `inherited-berlin-t8n`
- `inherited-cli-contract`
- `candidate-liveness`

This step's **scored** groups and their weights (they sum to 1.0):

- `t9n-valid` — **0.17**
- `t9n-invalid` — **0.17**
- `t9n-intrinsic-gas` — **0.17**
- `b11r-assembly` — **0.17**
- `b11r-ommers-and-roots` — **0.17**
- `tool-dispatch-and-errors` — **0.15**

So: break anything you inherited and the step is 0. Leave it all intact and do
none of the new work and the step is still 0 — the gate earns nothing. The only
way to score is to implement this step's capability, and every case of it you
get right counts.

If a run scores 0, `reward_details.json` says which it was: on a gate veto it
names the gate group and the cases that failed. A 0 with the gate marked
`passed` means the gate was fine and the new work is not there yet.

The scored groups are built by measurement, not by sampling: every case in them is one
that the **previous** step's finished tool gets *wrong* at this step's fork.
Inheriting the previous step's workspace and changing nothing therefore scores
exactly **0** — the gate passes, the score is zero. There is no way to bank a
step on work that was already done.

The gate's regression groups are the other side of that: they run the *earlier*
forks' corpora, so a change that fixes this fork by breaking an older one
zeroes the step immediately. Cases that only assert behaviour at the **previous**
fork, or that both binaries already satisfy, live in the gate too — they are
regression, not new capability, and they pay nothing.

## One thing in the tree that is not upstream

`github.com/fjl/memsize` is replaced, in `go.mod`, by
`./third_party/memsize-stub`. The upstream pin reaches into `runtime` through
`//go:linkname` and no longer links under a current Go toolchain, and
`internal/debug` — which imports it — is on `cmd/evm`'s import path, so without
the replacement nothing here builds. The stub reproduces the API surface and
does nothing; `memsize` is a debug HTTP handler and has no bearing on the state
transition.

Leave the replace directive alone unless you have a better fix. `go mod tidy`
preserves it.

## The checkpoint

At the end of the step the whole of `/app` is captured and handed to the next
step, excluding `.git/` and `build/`. Anything you leave in the tree — a helper
script, a note to yourself, a half-finished refactor — arrives at the next step.
Anything you put in `build/` does not; the next step rebuilds from source.
