# WORKSPACE_HANDOFF.md

How the workspace at `/app` is carried from one milestone to the next, and what
a fresh session must find when it arrives. This file describes **workspace
visibility only**. The product contract is in `TEST_INTERFACE.md` and the
normative semantics are in `icechunk-spec/`.

## What you inherit

`/app` is cumulative. The session working on milestone *n* starts from the
complete `/app` the session working on milestone *n-1* left behind, with this
milestone's documentation overlaid on top. Nothing is reset between milestones,
and nothing you delete comes back.

At milestone 01 the workspace contained a starter Cargo workspace with an empty
`icechunk` library. Arriving at this milestone it contains milestone 01's
finished implementation: the spec-1 repository format, its storage backends and
its Zarr-facing store. The documents under `icechunk-spec/`, plus
`TEST_INTERFACE.md` and this file, have just been replaced with this
milestone's editions — they are newer than the code beside them, and where the
two disagree the documents are what the next verifier holds you to.

## What you must preserve

Leave all of these in place, working, at the end of your milestone:

| Path | Why |
| --- | --- |
| `Cargo.toml` | the workspace root; `cargo build` here must succeed and must build the `icechunk` library as a default member |
| `Cargo.lock` | committed and consistent with the manifests; the verifier can fall back to it when resolving its own drivers |
| `icechunk/Cargo.toml` | the package must stay named `icechunk`; the verifier links `icechunk = { path = "/app/icechunk" }` |
| `icechunk/src/` | the cumulative implementation; every earlier milestone's behaviour must still work through this release's spelling |
| `icechunk-spec/` | the normative corpus; never edit a file in it |
| `TEST_INTERFACE.md` | this milestone's contract |
| `WORKSPACE_HANDOFF.md` | this file |
| `timer.sh` | the advisory countdown |

Do not move the crate out of `/app/icechunk`, do not rename the package, and do
not make `cargo build` at `/app` require a member the verifier image cannot
build. If you add workspace members, keep `default-members = ["icechunk"]`.

`/app/target/` is build output. It is excluded from the artifact that reaches
the next milestone and from the artifact the verifier grades, so the verifier
always rebuilds from source. Do not rely on anything under `target/` surviving.

## Checkpoint before you finish

```sh
cd /app
cargo build                 # must succeed
cargo build --offline       # should also succeed: your lockfile is complete
grep -n '^name' icechunk/Cargo.toml    # must print: name = "icechunk"
ls Cargo.toml Cargo.lock icechunk/Cargo.toml icechunk/src/lib.rs \
   TEST_INTERFACE.md WORKSPACE_HANDOFF.md icechunk-spec/spec.md
bash timer.sh               # advisory only
```

Leave the workspace so that a fresh session can run those commands and get the
same result. At the final milestone there is no next session, and the same
checkpoint is what an operator restoring the finished artifact runs. Write down
nothing else: there is no handoff note to produce, and a prose summary is not
read by anything.

## Restoring the workspace by hand

To reproduce this milestone by hand, restore the complete preceding
workspace snapshot first and only then overlay this milestone's documents:

```sh
cp -a <prior-artifacts>/workspace/. /app/
cp -a <task>/steps/02-sessions-conflicts-virtual-chunks/workdir/. /app/
```

A workdir is documentation, never a replacement repository: copying only the
workdir gives you an `/app` with no implementation, which fails the build before
any group runs. If you have no prior snapshot, the starting point is milestone
01's finished `icechunk` crate — reconstruct that first.
