# Workspace handoff — step 11 (release 0.23.22)

## What you have been given

`/app` is the workspace as the previous step left it, carried forward whole.
It already contains every earlier step's implementation; this step is an edit
to that tree. The previous step was `10-deframer-rewrite-and-early-data`.

Alongside the source you will find:

- `TEST_INTERFACE.md` — the contract the verifier holds you to. Read it before
  you rename anything.
- `docs/specs/` — the normative specifications, verbatim. `docs/specs/README.md`
  says which document answers which question.
- `timer.sh` — your remaining budget.

## Checkpointing your work

Build the same way the verifier will, and run the examples against each other:

```sh
cd /app
cargo build --locked -p bogo
cargo build --locked -p rustls-examples --bins
bash /app/timer.sh
```

The exact build line is in `TEST_INTERFACE.md` §1 — use it verbatim rather
than a shorter one of your own, because that is the one this step is written
against.
A useful end-to-end check is to start your example server in `echo` mode on a
loopback port and point your example client at it; if those two cannot talk to
each other, nothing else will work either.

## What must survive into the next step

This workspace is **cumulative**. It is handed to the next step exactly as you
leave it, and earlier milestones are re-checked there.

- Leave the tree in a state that builds with the line in `TEST_INTERFACE.md`.
- Do not delete or stub out behaviour you inherited in order to make this
  step's change simpler. A capability that worked before this step and does not
  work after it is a regression, and it is priced.
- `TEST_INTERFACE.md`, `docs/specs/` and `timer.sh` are supplied again at
  the start of every step and the fresh copies replace whatever is there,
  so nothing is lost if you move or delete them and nothing scores you on
  keeping them. What that also means is that edits you make to them do not
  survive: keep your own notes in a file of your own.
- `/app/test-ca` is replaced by the verifier. Do not store anything there.

## Restoring this workspace elsewhere

`/app` is self-contained: it needs no credentials, no network service and no
path outside itself. To continue this work in a fresh conversation, copy the
whole `/app` tree, then re-run the build line from `TEST_INTERFACE.md` §1 to
confirm the copy is complete. A `target/` directory is build output and can be
regenerated; everything else in `/app` is part of the artifact.
