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

At milestone 1 the workspace contains a starter Cargo workspace with no
implementation. At every later milestone — this one included — it contains the
previous milestone's finished implementation of the `icechunk` crate.

A milestone's `workdir` is **documentation**, never a replacement repository.

The overlay **writes same-named files and deletes none**, so `icechunk-spec/`
is a merged tree rather than this release's corpus alone: one page milestone 01
shipped and upstream has since dropped is still there. Read
`icechunk-spec/README.md` rather than assuming the corpus is exactly what this
release published — it describes the merged tree as you will find it — and
`PROFILE.md` P13 gives the rule for telling a leftover apart (`SHA256SUMS` lists
only the files this milestone copied) and marks it non-normative.

## What you must preserve

Leave all of these in place, working, at the end of your milestone:

| Path | Why |
| --- | --- |
| `Cargo.toml` | the workspace root; `members` must still contain `icechunk` and `default-members` must still be `["icechunk"]`, so `cargo build` in `/app` builds the library |
| `Cargo.lock` | the resolved graph; keep it consistent with the manifests so an offline build works |
| `icechunk/Cargo.toml` | the package must stay named `icechunk` |
| `icechunk/src/` | the cumulative implementation; every earlier milestone's behaviour must still work |
| `icechunk-spec/` | the normative corpus; **never edit a file in it** |
| `TEST_INTERFACE.md` | this milestone's contract |
| `WORKSPACE_HANDOFF.md` | this file |
| `timer.sh` | the advisory countdown |

Do not move the crate out of `/app/icechunk`, do not flatten the workspace into
a single package at `/app`, and do not rename the package. The verifier compiles
a driver crate that declares `icechunk = { path = "/app/icechunk" }`.

Other workspace members that arrived with the workspace may stay; they are
neither built by `cargo build` at the root (because of `default-members`) nor
graded. If one of them stops compiling it costs you nothing, but a member listed
in `[workspace] members` that cannot even be *parsed* breaks the root manifest,
and then everything fails.

`/app/target/` is build output. It is excluded from the artifact that reaches
the next milestone and from the artifact the verifier grades, so the verifier
always rebuilds from source. Do not rely on anything under `target/` surviving.

## This milestone changes the on-disk format

The metadata format changes incompatibly here. Nothing in the workspace carries
repository *data* between milestones — there is no fixture repository in `/app`
— so there is nothing to migrate. What matters for the handoff is only that the
crate still builds and that the public surface `TEST_INTERFACE.md` names is
present.

## Checkpoint before you finish

```sh
cd /app
cargo build                        # must succeed
cargo build --offline              # should also succeed: your lockfile is complete
grep -n '^name' icechunk/Cargo.toml        # must print: name = "icechunk"
grep -n 'default-members' Cargo.toml       # must print: default-members = ["icechunk"]
ls Cargo.lock icechunk/src/lib.rs TEST_INTERFACE.md WORKSPACE_HANDOFF.md \
   icechunk-spec/spec.md icechunk-spec/flatbuffers/snapshot.fbs
```

Leave the workspace so that a fresh session can run those commands and get the
same result. At the final milestone there is no next session, and the same
checkpoint is what an operator restoring the finished artifact runs. Write down
nothing else: there is no handoff note to produce, and a prose summary is not
read by anything.

## Restoring the workspace by hand

To reproduce a milestone by hand, restore the complete preceding
workspace snapshot first and only then overlay the milestone's documents:

```sh
cp -a <prior-artifacts>/workspace/. /app/
cp -a <task>/steps/04-flatbuffers-format/workdir/. /app/
```

Copying only the workdir gives you an `/app` with documents and no
implementation, which fails `cargo build` and therefore every scored group.
