# WORKSPACE_HANDOFF — step 11 of 11 (icechunk 2.2.0)

How the workspace travels between steps, what you must not delete, and — because
this is the **last** step — what a complete, finished artifact looks like.

This file does not define the product. `/app/TEST_INTERFACE.md` and
`/app/icechunk-spec/PROFILE.md` do that.

## What `/app` is

`/app` is a **cumulative workspace**. Each step runs in a fresh conversation
with no memory of the previous one, but on the *same* `/app` tree, with that
step's documentation overlaid on top. Everything you write under `/app` is what
the next step starts from — and at this step, what the final artifact is.

At the start of this step `/app` contains:

- the Rust workspace you (or a previous step) built: the root `Cargo.toml` and
  `Cargo.lock`, and the nine member crates `icechunk`, `icechunk-types`,
  `icechunk-format`, `icechunk-storage`, `icechunk-s3`,
  `icechunk-arrow-object-store`, `icechunk-python`, `icechunk-macros`,
  `icechunk-js` — plus anything else you added;
- `icechunk-spec/` — refreshed to this release's documentation corpus, with
  `PROFILE.md` and `README.md` for this step. This step contributes 79 files;
  because the overlay **merges**, the directory you see also carries 35
  superseded copies left by earlier steps, 114 files in all. `README.md` lists
  them and says what replaced each;
- `TEST_INTERFACE.md` — refreshed for this step;
- this file and `timer.sh`, both refreshed for this step.

The overlay **replaces** files with the same name. `icechunk-spec/`,
`TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md` and `timer.sh` are overlay-owned;
everything else is yours and is preserved exactly as you leave it.

## Preservation rules

1. **Do not delete or move `icechunk-spec/`, `TEST_INTERFACE.md`,
   `WORKSPACE_HANDOFF.md` or `timer.sh`.** The checkpoint below assumes they are
   at these paths.
2. **Keep the workspace building at every point you might be interrupted.** The
   snapshot is taken when the step's time runs out, not when you decide you are
   finished, so a workspace that does not build is a workspace that scores
   nothing. Reach a compiling state early and improve it in place.
3. `target/` is **excluded** from the snapshot, and so are `.git/`, `.venv/` and
   `node_modules/`. Sources and manifests are what carry forward; never treat a
   built artefact as the deliverable.
4. **Keep `Cargo.lock` committed at the workspace root, and keep it consistent
   with the manifests.** The verifier builds a separate driver crate against
   your `icechunk` by path, and it prefers a dependency resolution that neither
   side has to re-resolve. A manifest whose lockfile is stale costs build time
   and can cost groups.
5. Do not add generated code that cannot be rebuilt offline from the workspace,
   and do not add a dependency the image does not already have unless you also
   update `Cargo.lock`.
6. Keep the workspace in the layout `TEST_INTERFACE.md` §1 describes: nine
   members, default member `icechunk`, and exactly one `[[bin]]` target
   (`icechunk`, behind the `cli` feature).
7. **Do not raise `workspace.package.rust-version`.** It is `1.91.1` and the
   edition is `2024` (`TEST_INTERFACE.md` §1). The toolchain that builds this
   workspace is fixed, so raising the floor above it turns a working workspace
   into one that will not compile. `object_store` **must** move 0.13.2 → 0.14.0
   (§6.8); apart from that, change the dependency graph only where §6 asks.
8. **Never delete or truncate inherited source.** Grading at this step
   re-exercises every capability the workspace already had — a quarter of the
   step's weight is one end-to-end path through each of the ten earlier
   milestones — so removing an earlier feature to make a new one easier loses
   score twice.

## Checkpoint

Run this by hand before the step ends. It uses only what `TEST_INTERFACE.md`
already promises and costs a couple of minutes. Run it **at the start too**,
before you change anything: on that first run everything below passes except the
four lines marked *new at this step*, which is the inherited artifact working
correctly rather than a regression.

```sh
cd /app

# 1. the workspace and the library build
cargo build --manifest-path /app/Cargo.toml

# 2. the command-line binary builds behind its feature
cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk
ICK=$(ls -d /app/target/debug/icechunk)

# 3. a local-filesystem repository through the CLI
export XDG_CONFIG_HOME=$(mktemp -d)
REPO=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/icechunk"
# `RepositoryDefinition` is externally tagged, so the variant is a YAML TAG
# (`!LocalFileSystem`), not a nested mapping — TEST_INTERFACE.md §4.5.
printf 'repos:\n  demo: !LocalFileSystem\n    path: %s\n    config: {}\n' "$REPO" \
  > "$XDG_CONFIG_HOME/icechunk/cli-config.yaml"

"$ICK" repo create demo                 # exit 0; creates $REPO/repo and $REPO/snapshots/
"$ICK" branch list demo                 # exit 0; one line: "<20-char id>  main" (new at this step)
"$ICK" ancestry demo                    # exit 0; "Snapshot: …" then "Date: …"   (new at this step)
"$ICK" tag create demo v1 main          # exit 0                                  (new at this step)
"$ICK" tag list demo                    # exit 0; one line ending in "v1"         (new at this step)
"$ICK" config list                      # exit 0; YAML containing "LocalFileSystem"
"$ICK" snapshot list demo; echo "exit=$?"   # exit=2 — this subcommand was REMOVED at this release

# 4. the library's own tests, if you keep any
cargo test --manifest-path /app/Cargo.toml --lib
```

Lines 1 and 2 must pass at both moments, and so do `repo create` and
`config list`, which the previous checkpoint already had. Line 3's
`branch list`, `ancestry`, `tag create` and `tag list` are this step's new
subcommands, and `snapshot list` must *stop* being accepted — so on the first
run of the checkpoint, before you have made the change, those four exit `2`
("unrecognized subcommand") and `snapshot list` exits `0`. All five are expected
then and all five are failures at the end.

Run `bash /app/timer.sh` to see the remaining budget for this step.

## What a complete finished artifact looks like

This is the last step of the ladder, so nothing starts from what you leave —
the workspace *is* the deliverable. It is complete when all of the following
hold at once. Every one of them is stated in `/app/TEST_INTERFACE.md`; this is
the checklist, not a second contract.

**It builds, twice.**

- `cargo build` at `/app` succeeds and builds the `icechunk` library;
- `cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk`
  succeeds and produces an `icechunk` binary.

**It is one library that does ten milestones' work** (`TEST_INTERFACE.md` §2).
A repository can be created over in-memory or local-filesystem storage, written
through sessions and the Zarr-facing `Store`, committed, branched, tagged and
read at an earlier version; two concurrent sessions conflict and rebase;
history can be expired and collected; metadata files are verified FlatBuffers
buffers behind a 39-byte header; manifests split as configured; the CLI opens a
repository from its configuration file; virtual chunk references resolve only
through an authorized container; branches, tags, configuration, status, feature
flags and an operations log live in one table of contents; and expiration
records the transaction logs it pruned.

**It does this release's seven new things** (`TEST_INTERFACE.md` §3, §4):

- all four `inspect_*` entry points emit a `header` describing the file they
  read — writer, spec version, file type, compression;
- `ChunkPayload::length()` is public;
- deleting a chunk key that cannot exist is a no-op, while writing one is still
  an error;
- a zero-length chunk write is rejected for inline, virtual and materialized
  references alike;
- the object-metadata key constants carry their new letters-only values, with
  the old spellings kept beside them as `*_DEPRECATED`;
- garbage collection judges retention by the object store's clock, so a
  still-referenced transaction log survives a skewed one;
- `new_hf_storage` exists and configures the Hugging Face gateway.

**Its command line has the right shape** (`TEST_INTERFACE.md` §4): `ancestry`,
`branch` and `tag` exist with the grammar §4.2 gives, `snapshot list` does not,
output is plain text on stdout, and the exit statuses are 0 / 1 / 2 as §4.3
says.

**It carries the migration** (`TEST_INTERFACE.md` §6): eight storage
constructors take the two header vectors, `ObjectStoreBackend::mk_object_store`
takes a `Role`, `rewrite_manifests` takes `Option<usize>`, and `object_store`
is at 0.14.

**And it is reproducible**: `Cargo.lock` is present, consistent and committed;
nothing in the tree needs the network to rebuild; and no test-only dependency
leaked into a non-dev section of a manifest.

## Restoring this workspace elsewhere

A standalone continuation restores the complete preceding snapshot first and
overlays this step's documentation on top of it — never the other way round.
The overlay is documentation, not a repository: it carries `icechunk-spec/`,
`TEST_INTERFACE.md`, this file and `timer.sh`, and nothing else. If you find
yourself with the overlay but no `Cargo.toml`, the snapshot was not restored and
no amount of work in `/app` will produce a scoreable artifact.
