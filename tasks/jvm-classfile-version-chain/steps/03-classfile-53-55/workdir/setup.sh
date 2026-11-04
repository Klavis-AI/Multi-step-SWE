#!/usr/bin/env bash
# The workspace handoff belongs to whoever is working in this container: leave
# the one that is already here alone, and only lay down the template when there
# is nothing to read.
set -u
f=/app/WORKSPACE_HANDOFF.md
if [ ! -s "$f" ]; then
  cat > "$f" <<'HANDOFF_EOF'
# Workspace handoff

## What is here

| path | what it is |
| --- | --- |
| `jvm/lib/platform/` | the platform class library, compiled. The clean tree the build runs from holds a fresh copy of it, so do not change it. |
| `jvm/lib/platform-src/` | the Java source of that library. Its `native` methods are the methods the virtual machine has to provide. |
| `tests/` | a sample conformance corpus with the behaviour recorded from a reference JDK, plus `tests/run-tests.sh`. |
| `jvm/` | where the virtual machine goes: C sources and headers under `jvm/src/` (text only), the makefile at `jvm/Makefile`, object files and binaries anywhere else under `jvm/`, and `make -C /app/jvm` must produce `jvm/bin/java`. |

## Keep this current

State what is implemented, what is not, where the code lives, and how to build
and test it.

## A note on the build

`make -C /app/jvm` must work in a tree with every build product
removed, so the makefile has to work from `jvm/src/` and `jvm/Makefile` alone.
Objects left here from an earlier session do *not* get cleaned up between
sessions: if the makefile does not rebuild an object when a header that object
includes changes, a changed constant or structure layout can end up half-applied
in a launcher that works in this tree and fails when it is built from scratch.
HANDOFF_EOF
fi
rm -f /app/setup.sh
exit 0
