# WORKSPACE_HANDOFF.md — milestone 07, the asynchronous API release

The workspace at `/app` is cumulative. It already contains the six milestones
before this one, and the milestones after this one build on whatever you leave
behind. Nothing here is a scratch directory.

## What is already in place

```
/app
├── Cargo.toml            the workspace: members icechunk, icechunk-python,
│                         icechunk-macros; default-members = ["icechunk"]
├── Cargo.lock            committed; build with --locked for reproducibility
├── icechunk/             the crate under test
│   ├── Cargo.toml        name = "icechunk", edition 2024
│   ├── src/              the library
│   ├── src/bin/icechunk/ the command-line binary, required-features = ["cli"]
│   ├── flatbuffers/      the normative binary schemas
│   └── tests/            the crate's own integration tests
├── icechunk-python/      the PyO3 extension module and its Python package
├── icechunk-macros/      the crate's own test macros
├── icechunk-spec/        the pinned documentation corpus (read-only reference)
├── TEST_INTERFACE.md     this milestone's interface contract
├── WORKSPACE_HANDOFF.md  this file
└── timer.sh              your remaining budget
```

The crate version at the start of this milestone is `0.3.0`; this milestone
raises it to **`0.3.4`**.

## What this milestone changes, in one paragraph

The milestone's headline is the asynchronous API, and that work lives entirely
in `icechunk-python`: a second `async def` entry point beside every synchronous
one, named with an `_async` postfix, handing the *same* already-`async` Rust
future to the Python event loop. The Rust crate has been fully asynchronous
since long before this release, so on the Rust side this milestone is a
different, smaller set of changes: virtual chunk container prefixes must now end
in `/` and matching is exact, so an authorisation for one prefix no longer
reaches a sibling that merely shares its text; a conflict solver reference
widens to `Send + Sync`; `VirtualChunkLocation` becomes opaque; garbage
collection and chunk accounting gain mandatory memory and concurrency
parameters plus a dry run; snapshot traversal is renamed and re-typed; and the
default asset caches triple. `TEST_INTERFACE.md` §3 lists all thirty-two migration
items — twenty-one changed, six new, one internal and four pinned as
unchanged.

## Rules for this workspace

* **Do not delete or rewrite what earlier milestones built.** Later milestones
  and this milestone's own inherited-regression checks depend on it. If a
  signature must change, change it and migrate every call site in the
  workspace, including `icechunk-python` and `icechunk/tests/`.
* **Keep the workspace building.** `cargo build --locked` at `/app` must
  succeed, and so must
  `cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk`.
  A workspace that does not build scores nothing.
* **Keep `Cargo.lock` committed and consistent** with the manifests. If you add
  a dependency, update the lock in the same change. The verifier builds your
  workspace with `--locked` first (`TEST_INTERFACE.md` §4); a drifted lock is
  rescued only when the versions cargo would pick are already cached, so do not
  rely on it — and the next milestone inherits the lock either way.
* **Do not touch `/app/icechunk-spec/`.** It is a pinned, checksummed reference
  corpus. The documentation overlay is **merged** into `/app` at the start of
  every milestone rather than swapped in wholesale: files this milestone ships
  replace the previous one's copies name for name, and a document an earlier
  milestone shipped under a name this one does not reuse stays where it is.
  `icechunk-spec/README.md` lists the thirteen files in that position here and
  `PROFILE.md` §5 records that none of them is normative.
* **Do not add network or cloud dependencies to a code path the library needs
  at rest.** Everything this milestone adds must be exercisable with in-memory
  or local-filesystem storage.
* The crate's workspace lints warn on `unwrap`, `expect`, `panic`, `todo`,
  `unimplemented` and `dbg!` in library code. Keep them out of new code.

## Where to start

1. Read `/app/TEST_INTERFACE.md` end to end, especially §1 (what to build), §3
   (the migration table) and §5 (what is explicitly out of scope).
2. Read `/app/icechunk-spec/README.md`, then `PROFILE.md`, then
   `guide/async.md` and `guide/configuration.md`.
3. Work through §3's migration table before §1's new behaviour: several items
   there change a call site's *type* without changing whether it compiles, and
   those are the ones that are cheap to miss and expensive to find later.

## What you hand to the next milestone

A workspace that builds, whose library and binary both work, whose earlier
milestones still behave as they did, and whose virtual-chunk configuration
surface now refuses an unseparated prefix while normalising the persisted and
credential-keyed ones it inherits.
