# Workspace handoff

## What is here

`/app/isal_crypto` — Intel's isa-l_crypto at a pinned commit. It builds with
the project's own `Makefile.unx`:

```sh
cd /app/isal_crypto && make -f Makefile.unx lib -j"$(nproc)"
```

`bin/isa-l_crypto.a` is what the verifier's harness links against.

`/app/tools/measure.sh` — practice measurement tool, see `/app/BENCHMARK.md`.

`/app/BENCHMARK.md` — what is scored, how it is measured, and how to check both.

## State of this tree at the start of this milestone

The optimized implementation of one or more of the library's multi-buffer
hashing surfaces has been removed from this tree, and the public symbols for
those surfaces resolve to the portable C reference upstream ships in the same
directory — the scalar C files this milestone's `instruction.md` names,
reached through upstream's own `*_base_aliases.c` shim. Everything else — the
build system, the project's test suite, the perf drivers — is upstream and
intact.

Surfaces you optimized in earlier milestones are already here in the state you
left them, and they are part of this milestone's baseline.

## How to check your work

```sh
cd /app/isal_crypto
make -f Makefile.unx checks -j"$(nproc)" && make -f Makefile.unx check
bash /app/tools/measure.sh <stage>
```

## Rebuild after every change

The measure tool links the static library, so rerun the build before rerunning
`measure.sh` — otherwise you are measuring the previous build.
