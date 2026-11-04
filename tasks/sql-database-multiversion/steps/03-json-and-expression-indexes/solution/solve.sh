#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: apply the exact upstream release-to-release
# transition from SQLite 3.8.3 to SQLite 3.9.0 over the cumulative /app tree.
# Nothing is cloned, downloaded or fetched.
#
#   upstream release   SQLite 3.9.0, 2015-10-14
#   SQLITE_SOURCE_ID   a721fc0d89495518fe5612e2e3bbc60befd2e90d
#   licence            public domain

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

( cd "$solution_dir" && sha256sum -c SHA256SUMS )

cd "$app_dir"

# The transition is a plain unified diff over src/, shell/ and include/.
# --binary keeps it byte-exact and needs no Git object database; the
# workspace is not a Git repository.
rm -rf -- build
git apply --binary --whitespace=nowarn -p1 \
    "$solution_dir/release-3.8.3-to-3.9.0.patch"

cp -- "$solution_dir/Makefile" "$app_dir/Makefile"

test -f "$app_dir/src/sqlite3.c"
test -f "$app_dir/shell/shell.c"
test -f "$app_dir/include/sqlite3.h"
grep -q '#define SQLITE_VERSION *"3.9.0"' "$app_dir/include/sqlite3.h"

test -f "$app_dir/TEST_INTERFACE.md"
test -f "$app_dir/WORKSPACE_HANDOFF.md"
test -d "$app_dir/sqlite-doc"

printf 'applied SQLite 3.8.3 -> 3.9.0 (source id a721fc0d89495518fe5612e2e3bbc60befd2e90d)\n'
