# Workspace handoff

The next session starts fresh: it sees this file, the tree, and its own ticket, and
nothing else. Keep this file accurate.

## Layout

| path | what it is |
| --- | --- |
| `SPEC/` | normative material for the current milestone (read-only reference) |
| `tests/`, `runtest` | Redis's own Tcl test suite, to run against your build |
| `src/redis-server` | produced by `make`; the only entry point |
| (yours) | the Java sources and the `Makefile` you add |

## State

_(replace: what is implemented, what is not, where the code lives, how to build and
run it, what the last local test run reported, and anything the next session would
otherwise have to rediscover.)_
