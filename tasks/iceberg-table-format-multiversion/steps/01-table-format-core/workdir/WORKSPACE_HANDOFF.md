# WORKSPACE_HANDOFF.md

How the workspace at `/app` is carried from one milestone to the next, and what
a fresh session must find when it arrives. This file describes **workspace
visibility only**; the product contract is in `TEST_INTERFACE.md` and the
normative semantics are in `iceberg-spec/`.

## What you inherit

`/app` is cumulative. The session working on milestone *n* starts from the
complete `/app` the session working on milestone *n-1* left behind, with this
milestone's documentation overlaid on top. Nothing is reset between
milestones, and nothing you delete comes back.

At milestone 1 the workspace contains a starter crate with no implementation.
At every later milestone it contains the previous milestone's finished
implementation.

## What you must preserve

Leave all of these in place, working, at the end of your milestone:

| Path | Why |
| --- | --- |
| `Cargo.toml` | the crate must stay named `iceberg`; `cargo build` in `/app` must succeed |
| `Cargo.lock` | the verifier reuses your resolution when it compiles its drivers |
| `src/` | the cumulative implementation; every earlier milestone's behaviour must still work |
| `LICENSE`, `NOTICE` | Apache-2.0 notices for the specification corpus |
| `iceberg-spec/` | the normative corpus; never edit a file in it |
| `TEST_INTERFACE.md` | this milestone's contract |
| `WORKSPACE_HANDOFF.md` | this file |
| `timer.sh` | the advisory countdown |

Do not move the crate into a subdirectory, do not turn `/app` into a Cargo
workspace with the implementation in a member crate, and do not rename the
package. The verifier compiles `iceberg = { path = "/app" }`.

`/app/target/` is build output. It is excluded from the artifact that reaches
the next milestone and from the artifact the verifier grades, so the verifier
always rebuilds from source. Do not rely on anything under `target/`
surviving.

## Checkpoint before you finish

```sh
cd /app
cargo build                 # must succeed
cargo build --offline       # should also succeed: your lockfile is complete
grep -n '^name' Cargo.toml  # must print: name = "iceberg"
ls Cargo.lock src/lib.rs TEST_INTERFACE.md WORKSPACE_HANDOFF.md iceberg-spec/spec.md
```

Leave the workspace so that a fresh session can run those four commands and get
the same result. At the final milestone there is no next session, and the same
checkpoint is what an operator restoring the finished artifact runs. Write down nothing else: there is no handoff note to
produce, and a prose summary is not read by anything.

## Restoring the workspace by hand

To reproduce a milestone by hand, restore the complete preceding
workspace snapshot first and only then overlay the milestone's documents:

```sh
cp -a <prior-artifacts>/workspace/. /app/
cp -a <task>/steps/NN-<name>/workdir/. /app/
```

A workdir is documentation, never a replacement repository: copying only the
workdir gives you an `/app` with no implementation, which fails the first gate.
