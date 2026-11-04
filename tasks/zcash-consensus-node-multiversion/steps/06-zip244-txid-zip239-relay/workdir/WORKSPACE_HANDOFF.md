# WORKSPACE_HANDOFF.md — milestone 06 of 14

You are working inside `/app`, a Rust cargo workspace holding a Zcash
consensus node. This file describes the workspace itself: what is in it, what
carries forward, and how to pick the work up in a fresh session. It does not
restate the milestone — that is `instruction.md`, delivered with the task
— and it says nothing about how the work is checked.

## What `/app` is

A single cargo workspace. Its members, and what each is for:

| Crate | Role |
| --- | --- |
| `zebra-chain` | consensus data types, serialization, chain parameters, arithmetic |
| `zebra-consensus` | block and transaction verification services |
| `zebra-network` | the Zcash peer-to-peer protocol and peer set |
| `zebra-state` | the two-tier chain state (finalized on disk, non-finalized in memory) |
| `zebra-script` | a thin wrapper over the pinned C++ script interpreter |
| `zebra-rpc` | the JSON-RPC surface |
| `zebra-client`, `zebra-utils`, `zebra-test` | client scaffolding, tools, shared test helpers |
| `zebrad` | the node binary that wires the services together |
| `tower-batch`, `tower-fallback` | small `tower` middleware crates used by the verifiers |

Not every crate is populated at every milestone; some are stubs that later
milestones fill in.

Documents that travel with the workspace:

- `instruction.md` — this milestone's statement of work, delivered to you
  directly alongside this workspace (it is not inside `/app`).
- `/app/TEST_INTERFACE.md` — the interface contract for this milestone:
  module paths, exported names, signatures, formats, and the build command
  used against your work. **Replaced at every milestone.** Read the copy that
  is in `/app` now, not one you remember.
- `/app/WORKSPACE_HANDOFF.md` — this file.
- `/app/specs/` — the normative Zcash documents. `nu5.txt` is the Zcash
  Protocol Specification (NU5 edition) as searchable text;
  `zips/zip-NNNN.rst` are the numbered ZIPs; `PROVENANCE.md` records the exact
  revision of each. These are copied verbatim from upstream and are the source
  of truth for every consensus question.
- `/app/timer.sh` — `bash /app/timer.sh` prints elapsed and remaining time.
  Advisory; the harness timeout is authoritative.
- `/app/book/`, `/app/design/` — the upstream project's own prose.

## What carries forward

At the end of this milestone the **entire `/app` directory is captured**,
excluding only `.git/` and `target/`, and becomes the starting `/app` of the
next milestone. That includes your source changes, any files you add, the
`Cargo.toml`/`Cargo.lock` as you leave them, and the documents above.

Practical consequences:

- Leave the workspace building. The next milestone starts from exactly this
  directory and adds to it; a workspace that does not compile costs the next
  session its whole budget.
- Do not delete `/app/specs/`, `/app/TEST_INTERFACE.md`,
  `/app/WORKSPACE_HANDOFF.md` or `/app/timer.sh`.
- `target/` is discarded, so a compile cache does not survive. Do not rely on
  incremental state; do keep `Cargo.lock` accurate.
- `.git/` is discarded. Committing is harmless but nothing is carried by it,
  so do not encode work product in git objects alone.
- Anything you write outside `/app` — `/tmp`, `$HOME`, `/root` — is lost.

## Building, offline

Your image ships a pinned Rust toolchain and a pre-populated cargo registry
cache holding this workspace's whole dependency graph, and cargo is configured
to resolve from it without the network. **Your first build needs no network at
all.** Compiling the graph is still the single largest fixed cost in your
budget, so do it early:

```
cd /app
cargo build            # resolves from the cache; give it time
```

`--offline` works from the start for everything already resolved, which is
how you should check yourself:

```
cargo build --offline --locked
cargo test  --offline --locked -p <crate>
```

`--offline` must keep working throughout — that is the real constraint, and
it is about your dependency graph, not about the cache.
Concretely: do not add a third-party dependency that is not already somewhere
in `Cargo.lock`, and do not bump a dependency's version requirement past what
the lockfile pins — **unless this milestone's own `instruction.md` or
`TEST_INTERFACE.md` names a specific version to move to. When they do, that
bump is required, it is part of the milestone, and it overrides this rule.**
Adding a *path* dependency on another workspace member is fine. Adding a new
`[dev-dependencies]` entry that is already in the lock is fine. If you must
change a manifest, run `cargo metadata --offline --format-version 1 >/dev/null`
afterwards; it fails loudly when the workspace can no longer resolve without
the network.

The verifier tries `--offline --locked` first and falls back through
`--offline`, `--locked`, and finally no flags, taking the first that resolves.
A fallback is recorded, not penalised — but a workspace that only resolves
with a fresh re-resolve is not the graph this task pinned, so keep
`Cargo.lock` accurate.

Cap your own parallelism at `-j4` / `--jobs 4`. The container shares a host
with other work and an unbounded build will be throttled or killed.

## Restoring this workspace in a fresh session

If your session is interrupted and a new one starts against the same `/app`:

1. Re-read `instruction.md` — the milestone. (It is delivered alongside the
   workspace, not written into `/app`; if you no longer have it, ask for it.)
2. `cat /app/TEST_INTERFACE.md` — the interface contract, which is the only
   place names and signatures are promised.
3. `ls /app/specs/zips/` and `grep -n '<topic>' /app/specs/nu5.txt` — the
   normative text.
4. `cd /app && cargo build --offline --locked 2>&1 | tail -40` — the honest
   statement of where the work stands. Whatever it reports is the true
   starting point; earlier notes in the conversation may be stale. (Drop
   `--offline` if `$CARGO_HOME` has not been populated yet in this container;
   `target/` is discarded between sessions, so expect a full rebuild either
   way.)
5. `bash /app/timer.sh` — remaining budget.

There is no separate scratch directory to recover and no external service to
reconnect to. `/app` is the whole of the state.
