# WORKSPACE_HANDOFF.md

How the workspace at `/app` is carried from one milestone to the next, and what
a fresh session must find when it arrives. This file describes **workspace
visibility only**; the product contract is in `TEST_INTERFACE.md` and the
normative semantics are in `icechunk-spec/`.

## What you inherit

`/app` is cumulative. The session working on milestone *n* starts from the
complete `/app` the session working on milestone *n-1* left behind, with this
milestone's documentation overlaid on top. Nothing is reset between milestones,
and nothing you delete comes back.

At milestone 1 the workspace contains a **starter workspace with no
implementation**: a Cargo workspace root, a library manifest for the `icechunk`
package, an empty crate root, a resolved lockfile, and the documents listed
below. At every later milestone it contains the previous milestone's finished
implementation.

## What you must preserve

Leave all of these in place, working, at the end of your milestone:

| Path | Why |
| --- | --- |
| `Cargo.toml` | the workspace root; `cargo build` in `/app` must succeed and `default-members` must build the library |
| `Cargo.lock` | the verifier reuses your resolution when its own pinned one does not apply |
| `icechunk/Cargo.toml` | the package must stay named `icechunk` |
| `icechunk/src/` | the cumulative implementation; every earlier milestone's behaviour must still work |
| `icechunk-spec/` | the normative corpus; never edit, trim or add a file to it |
| `TEST_INTERFACE.md` | this milestone's contract |
| `WORKSPACE_HANDOFF.md` | this file |
| `timer.sh` | the advisory countdown |

Do not move the library out of `/app/icechunk`, do not rename the package, and
do not make `/app` something other than a Cargo workspace whose default member
is that library. The verifier compiles
`icechunk = { path = "/app/icechunk" }` from a crate of its own, outside `/app`.

`/app/target/` is build output. It is excluded from the artifact that reaches
the next milestone and from the artifact the verifier grades, so the verifier
always rebuilds from source. Do not rely on anything under `target/` surviving.

## Checkpoint before you finish

```sh
cd /app
cargo build                             # must succeed
cargo build --offline                   # should also succeed: your lockfile is complete
grep -n '^name' icechunk/Cargo.toml     # must print: name = "icechunk"
ls Cargo.toml Cargo.lock icechunk/src/lib.rs \
   TEST_INTERFACE.md WORKSPACE_HANDOFF.md icechunk-spec/spec.md
```

Leave the workspace so that a fresh session can run those four commands and get
the same result. At the final milestone there is no next session, and the same
checkpoint is what an operator restoring the finished artifact runs. There is no
handoff note to write: a prose summary is not read by anything.

## Restoring the workspace by hand

To reproduce a milestone by hand, restore the complete preceding
workspace snapshot first and only then overlay the milestone's documents:

```sh
cp -a <prior-artifacts>/workspace/. /app/
cp -a <task>/steps/NN-<name>/workdir/. /app/
```

A workdir is documentation plus, at milestone 1 only, an empty scaffold — never
a replacement repository. Copying only the workdir gives you an `/app` with no
implementation, which fails every scored group.
