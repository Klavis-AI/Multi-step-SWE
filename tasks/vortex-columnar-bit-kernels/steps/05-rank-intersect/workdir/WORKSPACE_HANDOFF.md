# Workspace handoff

## Where you are

`/app/vortex` is the Vortex columnar format library at a pinned upstream commit,
with the optimized implementation of some of its bit-level kernels removed. What
remains is the portable implementation of each of them, which is complete and
correct: the tree builds, computes the right answer on every input, and does not
pass its own suite clean — the failures it starts with are not held against you.

This is the starting checkpoint. `/app` is cumulative — whatever is in it when a
step ends is what the next step begins from — so keep the tree in a buildable
state and keep this file up to date.

## Build and check

```sh
cd /app/vortex
cargo build --release -p vortex-buffer -p vortex-mask -p vortex-array
cargo test  --release -p vortex-buffer -p vortex-mask -p vortex-array --lib
```

The tree is pre-warmed; an incremental build is seconds, not minutes. Do not add
dependencies — the grader builds offline against the registry this tree shipped
with.

## Your target

`BENCHMARK.md`, beside this file, names the exact entry points this milestone is
scored on and how they are timed. Read it before you start: the scored set is
small and named, and an entry point you leave alone costs you.

## What to leave behind

Update this file when you finish: what you changed, the speedup you measured
locally and with which command, and anything the next reader needs to rebuild the
tree. Leave `/app/vortex` buildable.
