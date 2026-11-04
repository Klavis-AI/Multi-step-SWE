# Workspace handoff

## What is here

```
/app/mlkem-native      mlkem-native at a pinned upstream tag, with the
                       optimized AVX2 arithmetic backend removed
/app/build             build directory (created by you)
/app/timer.sh          remaining time in this step's budget
/app/BENCHMARK.md      how to build, how behaviour is checked, how speed is
                       measured
/app/WORKSPACE_HANDOFF.md   this file
```

`/app/mlkem-native` is the project: its own `Makefile`, its tests and its
reference documentation. Every **arithmetic** backend was removed (x86_64,
aarch64, ppc64le, riscv64) along with `proofs/`, `examples/`, `dev/` and
`scripts/autogen`; comments and CI files that still name those paths are inert.
One native backend remains and is outside this milestone's scope: the
FIPS-202/Keccak one, which must be left alone.

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
guards around the unit test's helper generators. **At this first milestone none
of those toggles is defined yet, so switching yours on is exactly the case that
trips it** — `BENCHMARK.md` says what the fix is, and that it is not graded.

## Your starting point

This is the first milestone of a cumulative workspace. Nothing has been
optimized yet: the arithmetic backend's hooks all return
`MLK_NATIVE_FUNC_FALLBACK` and every primitive runs the portable C code.

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

Make `mlk_poly_rej_uniform` faster without changing any output byte. The
dispatch hook for it is `mlk_rej_uniform_native()` in
`mlkem/src/native/x86_64/meta.h`; its contract is in
`mlkem/src/native/api.h`. `BENCHMARK.md` has the commands.

## Handoff

Leave this file updated when you finish: what you changed, the speedup you
measured locally, the command you measured it with, and anything you know is
incomplete. It is read by whoever inspects this milestone's artifact; it is not
carried forward, since the file is regenerated.
