# WORKSPACE_HANDOFF.md — milestone 06

How this workspace reaches you, what you must leave behind, and how to pick the
work up again in a fresh conversation.

## 1. What is in `/app`

`/app` is a **cumulative Rust workspace**. It already contains the
implementation produced by milestones 01–05; this milestone extends it in
place. Nothing was reset between milestones, and nothing you find here is
scaffolding to be replaced.

```
/app
├── Cargo.toml            workspace manifest
├── Cargo.lock            committed; keep it in step with the manifests
├── icechunk/             the library crate, and the `icechunk` binary
├── icechunk-python/      the PyO3 bindings crate, carried forward; the
│                         verifier never builds it
├── icechunk-spec/        documentation, read-only (see §4)
├── TEST_INTERFACE.md     the public contract for this milestone
├── WORKSPACE_HANDOFF.md  this file
└── timer.sh              elapsed / remaining budget
```

The workspace manifest arrives with `members = ["icechunk",
"icechunk-python"]` and `default-members = ["icechunk"]`, so a bare
`cargo build` at `/app` builds the library alone. This milestone **adds a third
member**, the `icechunk-macros` proc-macro crate — `TEST_INTERFACE.md` §1 says
what it is for and why nothing is scored on it. Further workspace members may
exist or be added; `default-members` must keep building with a bare
`cargo build`.

A documentation overlay is copied into `/app` at the start of every milestone.
It is **merged** into the tree, not swapped in wholesale: it owns
`TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md`, `timer.sh` and the files of
`icechunk-spec/` that this milestone ships, each of which replaces the previous
milestone's copy **name for name**. A document an earlier milestone shipped
under a name this one does not reuse therefore stays where it is;
`icechunk-spec/README.md` lists the thirteen files in that position here and
records that none of them is normative. Everything else in `/app` is yours and
survives.

## 2. What you must preserve

1. **Everything earlier milestones built keeps working.** This is cumulative
   work: a change that trades an inherited behaviour for a new one is a
   regression, not a refactor. Where an inherited signature genuinely had to
   change at this release, `TEST_INTERFACE.md` §5 says so explicitly and gives
   both spellings; anything not listed there must keep behaving as it did.
2. **`cargo build` at `/app` must succeed**, and so must
   `cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk`.
   A workspace that does not build is worth nothing regardless of what is in
   it.
3. **Keep `/app/Cargo.lock` consistent with the manifests.** If you add or
   change a dependency, commit the resulting lockfile along with it.
4. **Do not delete or rewrite `/app/icechunk-spec/`.** It is copied
   documentation; see §4.
5. **Leave the tree clean.** Remove scratch files, experiment directories and
   dead code before you finish. `target/` is excluded from the collected
   artifact, so you do not need to clean it, but you also cannot rely on
   anything inside it surviving.

## 3. Checkpointing, and picking the work up again

Check your remaining budget at any time:

```bash
bash /app/timer.sh
```

Your work is collected from `/app` when the milestone ends, so **the filesystem
is the handoff**. There is no other channel: notes in your conversation, in
your scratch directory outside `/app`, or in a chat summary do not travel.

Useful habits, in rough priority order:

```bash
# Does it still build, at both entry points?
cargo build --manifest-path /app/Cargo.toml
cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk

# Does your own test suite still pass? (Your tests are for you; they are not
# collected as evidence and are never used to decide the milestone.)
cargo test --manifest-path /app/Cargo.toml

# What changed, if the tree is a git repository?
git -C /app status --short
git -C /app diff --stat
```

If `/app` is a git repository, committing as you go is the cheapest way to
leave a legible trail. If it is not, do not initialise one just for this — a
clean, building tree is what matters.

To restore this work in a fresh conversation, a new session needs only `/app`
as you left it. Anything a successor must know that is not obvious from the
code — a deliberate deviation, a half-finished migration, a decision you made
and its reason — belongs in a short `NOTES.md` at `/app`, or in comments next
to the code it explains. Do not put such notes in `TEST_INTERFACE.md`,
`WORKSPACE_HANDOFF.md`, `timer.sh` or anywhere under `icechunk-spec/`: those
are overlay-owned, the next milestone overwrites them, and your text would be
lost.

## 4. `/app/icechunk-spec/` is read-only

That directory holds documentation copied verbatim from the upstream project,
plus two task-written navigation files:

* `README.md` — what is in the corpus and the order to read it in;
* `PROFILE.md` — every place where those documents and the required behaviour
  disagree, and which one wins.

Start with those two. Treat every other file there as immutable: do not edit,
trim, annotate or "fix" a copied document, even where it is out of date.
`PROFILE.md` is where such disagreements are already recorded, and
`TEST_INTERFACE.md` is where the binding contract lives.

The corpus contains no implementation — no Rust, no Python, no build system.

## 5. Scope boundaries

* Work only inside `/app`. Do not modify anything under `/tests`, and do not
  read it, later milestones' material, or solution material.
* You do not have network access, so an existing implementation of this
  project or an answer to this task cannot be found, downloaded, copied,
  vendored or wrapped.
