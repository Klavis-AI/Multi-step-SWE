# Workspace handoff

This directory is the repository for one piece of software: a Lua implementation
written in Java, carried forward across successive milestones. Each milestone starts
a fresh session that sees only this directory and that milestone's instruction,
so everything the next session needs has to be *here*.

## Layout

| Path | What it is |
| --- | --- |
| `SPEC/lua-<version>/` | the reference manual for the release this milestone targets, as published by lua.org |
| `tests/lua-<version>-tests/` | that release's official test suite, as published by lua.org |
| `tests/conformance/` | conformance programs with the exact output the reference implementation produces for them |
| `tests/run-conformance.sh` | runs the conformance programs against `./lua` and reports the mismatches |
| `timer.sh` | advisory countdown for this milestone's budget |
| everything else | yours |

## Contract

* `make`, run here, builds the interpreter and produces an executable `./lua`.
* `make` honours `MYCFLAGS`: its contents are passed to the build as extra
  compile-time macro definitions, as `src/Makefile` does in the reference
  distribution.
* `./lua` is the standalone interpreter described in the "Lua Stand-alone"
  section of the reference manual for the release being built.
* The implementation is Java, built with the JDK installed in this image, with no
  third-party libraries, no native code and no external processes.

## Leaving a usable handoff

Keep the build green (`make clean && make` must work), keep `SPEC/` and `tests/`
in place, and record in your own notes file anything a fresh session would
otherwise have to rediscover — where the pieces of the implementation live, what
is finished, and what is known to be missing.
