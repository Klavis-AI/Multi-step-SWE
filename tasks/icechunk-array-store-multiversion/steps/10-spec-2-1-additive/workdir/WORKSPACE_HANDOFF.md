# WORKSPACE_HANDOFF.md — step 10, "spec 2.1"

How this workspace is carried between sessions. It describes visibility and
preservation only; the product contract is in `/app/TEST_INTERFACE.md` and the
normative documents are under `/app/icechunk-spec/`.

## What `/app` is

`/app` is **one cumulative workspace**, carried forward from the previous
milestone and handed to the next one. It is not reset between steps. At the
start of this session it holds:

* the Rust workspace you inherited — `Cargo.toml`, `Cargo.lock`, and the nine
  member crates, with the previous milestone's implementation already in place;
* `/app/icechunk-spec/` — this milestone's specification corpus, refreshed for
  this step (see `/app/icechunk-spec/README.md`);
* `/app/TEST_INTERFACE.md` — this milestone's public contract;
* `/app/WORKSPACE_HANDOFF.md` — this file;
* `/app/timer.sh` — the budget reporter.

The four files above are **overlaid fresh at the start of every step**, under
the same names. Anything you write into one of them is replaced, so keep your
own notes in a file of your own choosing — `/app/NOTES.md`, say — which is
carried forward untouched.

## What is collected, and what is not

Only `/app` is collected and restored for the next step and for grading. These
paths are **excluded** from the collected artifact:

```
.git  .git/**  target  target/**  **/target/**  .venv  .venv/**  node_modules  **/node_modules/**
```

So build output is never carried; the next session and the verifier both rebuild
from source. Everything else you leave in `/app` survives, including files you
add.

## Preservation rules

1. **Never delete or truncate inherited source.** The next milestone builds on
   it, and this milestone's grading re-exercises every capability the workspace
   already had. Removing an earlier feature to make a new one easier loses score
   twice.
2. **Keep the workspace buildable.** `cargo build` at `/app` must succeed with
   the default feature set, and
   `cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk`
   must produce the `icechunk` binary.
3. **Leave the dependency graph as you found it, and let cargo maintain
   `Cargo.lock`.** Nothing this milestone is **graded** on needs a new
   third-party crate: the one feature that would pull new crates in — the
   non-default `otel` feature and its OpenTelemetry exporter — is explicitly not
   scored (`TEST_INTERFACE.md` §11), and a dependency-free stub feature whose
   `shutdown_telemetry()` is a no-op loses nothing. Grading compiles a
   verifier-owned driver crate `--locked`, and it chooses its resolution
   **before** compiling any driver code: it prefers its own task-owned lockfile
   (`--offline --locked`, resolving and downloading nothing), and only if that
   does not fit your manifests does it fall back to seeding from your
   `/app/Cargo.lock`, and then to a full re-resolution. All three are supported
   and which one is used is **not scored** — see `TEST_INTERFACE.md` §1. So
   changing the graph is not forbidden, it is just slower and riskier: keep
   `/app/Cargo.lock` a **regular, committed file** that cargo wrote (never
   hand-edited, never deleted, never a symlink — any of those makes the verifier
   skip the fallback), and never ship a manifest or lockfile no resolution can
   satisfy, which costs every scored group at once.
4. **Do not raise `workspace.package.rust-version`.** It is `1.91.1` at this
   milestone.
5. **Do not vendor or check in build output**, and do not add submodules.

## Checkpointing during the session

`/app` is not a git repository and nothing auto-saves. Checkpoint whenever you
reach a state you would not want to lose:

```bash
cargo build --manifest-path /app/Cargo.toml                                  # library
cargo build --manifest-path /app/icechunk/Cargo.toml --features cli --bin icechunk
cargo test  --manifest-path /app/icechunk/Cargo.toml --lib                   # in-crate unit tests
bash /app/timer.sh                                                           # remaining budget
```

The crate's own integration tests under `/app/icechunk/tests/` are a single
binary (`icechunk/tests/main.rs`, `harness = false`) and several of its modules
need MinIO, rustfs or cloud credentials that are not available here. Prefer
`--lib` for a quick check, or run a single integration test by name. The crate
also sets `autotests = false`, so a new file under `icechunk/tests/` is silently
not compiled unless you add it as a `mod` in `tests/main.rs` or give it its own
`[[test]]` entry.

If you want a snapshot you can roll back to, copy the tree inside `/app`:

```bash
cp -a /app/icechunk /app/.checkpoint-icechunk-$(date +%s)
```

Remove such copies before the session ends — they are collected too, and a stale
duplicate of the crate in the artifact is confusing for the next session.

## Restoring a standalone continuation

If a session is resumed in a fresh conversation with the same `/app`:

1. Read `/app/TEST_INTERFACE.md` first: it is the complete public contract for
   this milestone, including what is deliberately out of scope.
2. Read `/app/icechunk-spec/README.md` for the map of the specification corpus
   and `/app/icechunk-spec/PROFILE.md` for the resolutions this task has already
   fixed.
3. Run the two build commands above to see where the workspace stands.
4. `grep -rn "todo!\|unimplemented!\|TODO(step-10)" /app/icechunk*/src` to find
   anything left half-done.

## Handing off

Leave the workspace building, with the milestone's capability implemented behind
the exact public paths and signatures in `/app/TEST_INTERFACE.md`. A brief
`/app/NOTES.md` recording what you changed, what you deliberately did not, and
anything the next milestone should know is welcome but not graded.
