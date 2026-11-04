#!/usr/bin/env bash
set -euo pipefail

# Oracle for milestone 1: install the official SQLite 3.8.0 release
# amalgamation over the seeded scaffold. Nothing is cloned, downloaded or
# fetched; the payload beside this script is the whole input.
#
#   upstream release   SQLite 3.8.0, 2013-08-26
#   SQLITE_SOURCE_ID   f64cd21e2e23ed7cff48f7dafa5e76adde9321c2
#   licence            public domain

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Verify the payload before touching the workspace.
( cd "$solution_dir" && sha256sum -c SHA256SUMS )

cd "$app_dir"

# Replace the scaffold's implementation directories. Everything else the
# seeded workspace supplied - sqlite-doc/, TEST_INTERFACE.md,
# WORKSPACE_HANDOFF.md, timer.sh - is preserved.
rm -rf -- src shell include build
mkdir -p -- src shell include

tar -xzf "$solution_dir/sqlite-3.8.0-amalgamation.tar.gz" -C "$app_dir"

# The amalgamation ships no build file, so install the task-owned Makefile
# that realises the documented `make` contract.
cp -- "$solution_dir/Makefile" "$app_dir/Makefile"

# Assert the result rather than trusting the extraction.
test -f "$app_dir/src/sqlite3.c"
test -f "$app_dir/shell/shell.c"
test -f "$app_dir/include/sqlite3.h"
test -f "$app_dir/include/sqlite3ext.h"
test -f "$app_dir/Makefile"
grep -q '#define SQLITE_VERSION *"3\.8\.0"' "$app_dir/include/sqlite3.h"

# The seeded documentation must survive the transition.
test -f "$app_dir/TEST_INTERFACE.md"
test -f "$app_dir/WORKSPACE_HANDOFF.md"
test -d "$app_dir/sqlite-doc"

printf 'installed SQLite 3.8.0 amalgamation (source id f64cd21e2e23ed7cff48f7dafa5e76adde9321c2)\n'
