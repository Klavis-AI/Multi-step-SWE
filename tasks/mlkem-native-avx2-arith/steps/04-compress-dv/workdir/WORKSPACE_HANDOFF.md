# Workspace handoff

## What is here

```
/app/mlkem-native      mlkem-native at a pinned upstream tag; the x86_64
                       arithmetic backend is partly restored by earlier
                       milestones, this stage's kernels are not
/app/build             build directory (created by you)
/app/timer.sh          remaining time in this step's budget
/app/BENCHMARK.md      how to build, how behaviour is checked, how speed is
                       measured
/app/WORKSPACE_HANDOFF.md   this file
```

`/app/mlkem-native` is the project: its own `Makefile`, its tests and its
reference documentation. The aarch64, ppc64le and riscv64 arithmetic backends
were removed, along with `proofs/`, `examples/`, `dev/` and `scripts/autogen`;
comments and CI files that still name those paths are inert. **The x86_64
arithmetic backend is present and partly restored** — earlier milestones'
kernels and toggles are in the tree. The FIPS-202/Keccak backend is also present
and is outside this milestone's scope: leave it alone.

## Build and test

```bash
cd /app/mlkem-native
make lib OPT=1 AUTO=1 -j4 BUILD_DIR=/app/build     # static libraries
make run_func run_kat OPT=1 AUTO=1 -j4 BUILD_DIR=/app/build
```

`make help`-style discovery is not wired up; the useful targets are `lib`,
`func`, `kat`, `run_func`, `run_kat`, `bench_components` (needs `CYCLES=PMU`)
and `clean`. All of them take `BUILD_DIR=` to keep build trees out of the
source checkout. **The ABI-conformance harness *is* part of this tree** — `test/abicheck/`
with 41 per-kernel checks across x86_64, aarch64, ppc64le and armv81m — but its
targets cannot link here, because the kernels they check include the ones **not
yet restored** — this stage's own, and later stages'. `make run_abicheck` therefore fails to link, and **its error output prints the
reference symbol names**; `make test` exists as a target and pulls in the same
tree. Both
are off the graded path — the graded suites are named above. Treat the abicheck
tree as part of the removed implementation: it names each reference kernel's
symbol, arity and ABI, so read it only if you want the interface, not the answer. `make unit` compiles on this tree as you receive it, but **may** stop
compiling once a native toggle is switched on: some toggles are missing from the
guards around the unit test's helper generators, and the earlier milestones'
toggles are already in those guards, so this depends on which toggles you add.
Check rather than assume; `BENCHMARK.md` says what to do and notes it is not
graded.

## Your starting point

This workspace is cumulative and this is not the first milestone: the tree you
inherit already contains the earlier milestones' work, and that work is part of
this milestone's baseline. **What you inherit is the tree, not this document:**
this file is regenerated for each milestone, so any notes left in it are not
carried forward — do not rely on reading a predecessor's notes here.

The build system compiles anything you add under
`mlkem/src/native/x86_64/src/` automatically: when `OPT=1` the Makefile
wildcards `*.c` and `*.S` from that directory, so a new kernel file needs no
build-system edit. **Use `.c` or `.S` — not a lowercase `.s`.** The wildcard
would pick a `.s` file up, but the build rules only cover `%.c.o` and `%.S.o`, so
`make lib` fails with "No rule to make target".

## How to rebuild after a change

```bash
cd /app/mlkem-native
make lib OPT=1 AUTO=1 -j4 BUILD_DIR=/app/build
```

`make` tracks the flags it was configured with, and changing them triggers a
rebuild on its own. A stale build directory is the most common way to measure
the tree you had rather than the tree you wrote — delete `BUILD_DIR` if a
result looks unchanged.

## What this milestone asks for

Make `mlk_poly_compress_dv`, in all three parameter sets faster without changing any output byte. The dispatch
hooks are named in `instruction.md`; their contracts are in
`mlkem/src/native/api.h`. `BENCHMARK.md` has the commands.

## Handoff

Leave this file updated when you finish: what you changed, the speedup you
measured locally, the command you measured it with, and anything you know is
incomplete. It is read by whoever inspects this milestone's artifact; it is not
carried forward, since the file is regenerated.
