# WORKSPACE_HANDOFF.md — milestone 05

How the workspace at `/app` is carried from one milestone to the next, and what
a fresh session must find when it arrives. This file describes **workspace
visibility only**; the product contract is in `TEST_INTERFACE.md` and the
normative semantics are in `icechunk-spec/`.

## What you inherit

`/app` is cumulative. The session working on milestone *n* starts from the
complete `/app` the session working on milestone *n-1* left behind, with this
milestone's documentation overlaid on top. Nothing is reset between milestones,
and nothing you delete comes back.

You are arriving at milestone 05, so `/app` already contains milestone 04's
finished implementation: the Icechunk Rust workspace with the `icechunk`
library crate, its FlatBuffers-backed format, sessions, conflict resolution,
expiration and garbage collection.

## What `/app` looks like

```text
/app/
  Cargo.toml            workspace manifest: members = ["icechunk", "icechunk-python"],
                        default-members = ["icechunk"]
  Cargo.lock            the resolved graph for the whole workspace
  icechunk/             the crate under test  (name = "icechunk")
    Cargo.toml
    src/
  icechunk-python/      carried forward; the verifier never builds it
  icechunk-spec/        the normative corpus for THIS milestone (read-only)
  TEST_INTERFACE.md     this milestone's contract
  WORKSPACE_HANDOFF.md  this file
  timer.sh              the advisory countdown
```

The documentation overlay is **merged** into `/app` at every milestone, not
swapped in wholesale: `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md`, `timer.sh`
and every file of `icechunk-spec/` this milestone ships replace the previous
milestone's copies name for name, while a document an earlier milestone shipped
under a name this one does not reuse stays where it is —
`icechunk-spec/README.md` names the one file in that position here, and records
that it is not normative. Everything else is yours and survives.

## What you must preserve

Leave all of these in place, working, at the end of your milestone:

| Path | Why |
| --- | --- |
| `Cargo.toml` | the workspace must keep `default-members = ["icechunk"]`; `cargo build` in `/app` must succeed |
| `Cargo.lock` | the verifier's own resolution is attempted against it |
| `icechunk/Cargo.toml` | the package must stay named `icechunk`, version `0.2.11`, edition `2024` |
| `icechunk/src/` | the cumulative implementation; every earlier milestone's behaviour must still work |
| `icechunk-spec/` | the normative corpus; **never edit a file in it** |
| `TEST_INTERFACE.md` | this milestone's contract |
| `WORKSPACE_HANDOFF.md` | this file |
| `timer.sh` | the advisory countdown |

Do not move the crate out of `/app/icechunk`, do not rename the package, and do
not remove `icechunk` from `default-members`. The verifier compiles
`icechunk = { path = "/app/icechunk" }` and builds
`--manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk`.

`/app/target/` is build output. It is excluded from the artifact that reaches
the next milestone and from the artifact the verifier grades, so the verifier
always rebuilds from source. Do not rely on anything under `target/`
surviving — including the `icechunk` binary, which the verifier rebuilds
itself.

## New at this milestone

This milestone adds a **binary target** to the crate. It is gated behind a
non-default feature, so it must be declared such that:

```sh
cargo build                                     # in /app: library only, no binary
cargo build --manifest-path icechunk/Cargo.toml --features cli --bin icechunk
```

both succeed, and the second produces `target/debug/icechunk`. Keep the
`icechunk` package free of a `default` feature: a default that turns the CLI on
would make the first command build the binary too, which is not what the
upstream layout does and not what the checkpoint below expects.

## Checkpoint before you finish

```sh
cd /app
cargo build                                                   # must succeed
cargo build --offline                                         # should also succeed
cargo build --manifest-path icechunk/Cargo.toml \
            --features cli --bin icechunk                     # must succeed
test -x target/debug/icechunk                                 # must exist
target/debug/icechunk --help                                  # must exit 0
grep -n '^name' icechunk/Cargo.toml                           # must print: name = "icechunk"
ls Cargo.lock icechunk/src/lib.rs TEST_INTERFACE.md \
   WORKSPACE_HANDOFF.md icechunk-spec/spec.md
```

Leave the workspace so that a fresh session can run those commands and get the
same result. Write down nothing else: there is no handoff note to produce, and
a prose summary is not read by anything.

## Restoring the workspace by hand

To reproduce a milestone by hand, restore the complete preceding
workspace snapshot first and only then overlay the milestone's documents:

```sh
cp -a <prior-artifacts>/workspace/. /app/
cp -a <task>/steps/05-cli-expiration-accounting-stores/workdir/. /app/
```

A workdir is documentation, never a replacement repository: copying only the
workdir gives you an `/app` with no implementation, which fails the first
build.
