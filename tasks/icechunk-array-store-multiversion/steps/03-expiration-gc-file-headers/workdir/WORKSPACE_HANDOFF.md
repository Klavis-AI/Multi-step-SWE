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

At milestone 1 the workspace contained a starter workspace with an empty
`icechunk` crate. At this milestone it contains milestone 2's finished
implementation: the `icechunk` library crate at `/app/icechunk`, a workspace
manifest at `/app/Cargo.toml` whose `default-members` is `["icechunk"]`, and a
committed `/app/Cargo.lock`.

The documents overlaid for this milestone are `TEST_INTERFACE.md`, this file,
and the whole of `icechunk-spec/`. The specification corpus **grew** at this
milestone — `design/004-binary-format.md`, `design/005-manifest-split.md`,
`design/006-tag-delete.md` and `design/007-basic-expiration.md` are new, and so
are the two guide pages `icechunk-python-parallel.md` and
`icechunk-python-faq.md`. An overlay **writes same-named files but deletes
none**, so the directory also still holds one page milestone 1 shipped and
upstream has since dropped. Read `icechunk-spec/README.md` rather than assuming
the corpus is what it was: it lists the merged tree as you will find it, and
`PROFILE.md` §D11 marks the superseded page non-normative.

## What you must preserve

Leave all of these in place, working, at the end of your milestone:

| Path | Why |
| --- | --- |
| `Cargo.toml` | the workspace manifest; `default-members` must stay `["icechunk"]` |
| `Cargo.lock` | the verifier prefers its own pinned resolution, but falls back to yours; an inconsistent lockfile can cost every group |
| `icechunk/Cargo.toml` | the package must stay named `icechunk`; its `version` is what lands in every file header |
| `icechunk/src/` | the cumulative implementation; every earlier milestone's behaviour must still work |
| `icechunk-spec/` | the normative corpus, including `LICENSE`, `NOTICE` and `SHA256SUMS`; never edit a file in it |
| `TEST_INTERFACE.md` | this milestone's contract |
| `WORKSPACE_HANDOFF.md` | this file |
| `timer.sh` | the advisory countdown |

Do not move the crate out of `/app/icechunk`, do not rename the package, and do
not add a second default workspace member. The verifier compiles
`icechunk = { path = "/app/icechunk" }` from a driver crate outside `/app`, and
it builds your workspace with `cargo build` run in `/app`.

Adding dependencies to `/app/icechunk/Cargo.toml` is allowed and expected — this
milestone needs a Zstandard codec, for instance. Keep `Cargo.lock` in step with
whatever you add, and keep the crate buildable offline from that lockfile where
you can: the verifier's registry cache is pre-populated with the dependency
graph this milestone ships with, and anything you add beyond it is fetched at
grading time.

`/app/target/` is build output. It is excluded from the artifact that reaches
the next milestone and from the artifact the verifier grades, so the verifier
always rebuilds from source. Do not rely on anything under `target/` surviving.

## Checkpoint before you finish

```sh
cd /app
cargo build                              # must succeed
cargo build --locked                     # your lockfile must be consistent
grep -n '^name'    icechunk/Cargo.toml   # must print: name = "icechunk"
grep -n '^version' icechunk/Cargo.toml   # must print: version = "0.1.0"
ls Cargo.lock Cargo.toml icechunk/src/lib.rs \
   TEST_INTERFACE.md WORKSPACE_HANDOFF.md \
   icechunk-spec/spec.md icechunk-spec/design/004-binary-format.md
```

Leave the workspace so that a fresh session can run those commands and get the
same result. At the final milestone there is no next session, and the same
checkpoint is what an operator restoring the finished artifact runs. Write down
nothing else: there is no handoff note to produce, and a prose summary is not
read by anything.

## Restoring the workspace by hand

To reproduce a milestone by hand, restore the complete preceding
workspace snapshot first and only then overlay this milestone's documents:

```sh
cp -a <prior-artifacts>/workspace/. /app/
cp -a <task>/steps/03-expiration-gc-file-headers/workdir/. /app/
```

A workdir is documentation, never a replacement repository: copying only the
workdir gives you an `/app` with no implementation, which fails the build before
any group runs. The overlay direction matters too — the workspace goes down
first, the documents on top — because the documents are what this milestone
changed.
