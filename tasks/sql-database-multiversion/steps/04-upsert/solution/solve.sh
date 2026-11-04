#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: apply the exact upstream release-to-release
# transition from SQLite 3.9.0 to SQLite 3.24.0 over the cumulative /app tree.
# Nothing is cloned, downloaded or fetched.
#
#   upstream release   SQLite 3.24.0, 2018-06-04
#   SQLITE_SOURCE_ID   c7ee0833225bfd8c5ec2f9bf62b97c4e04d03bd9566366d5221ac8fb199a87ca
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
    "$solution_dir/release-3.9.0-to-3.24.0.patch"

cp -- "$solution_dir/Makefile" "$app_dir/Makefile"

test -f "$app_dir/src/sqlite3.c"
test -f "$app_dir/shell/shell.c"
test -f "$app_dir/include/sqlite3.h"
grep -q '#define SQLITE_VERSION *"3.24.0"' "$app_dir/include/sqlite3.h"

test -f "$app_dir/TEST_INTERFACE.md"
test -f "$app_dir/WORKSPACE_HANDOFF.md"
test -d "$app_dir/sqlite-doc"

printf 'applied SQLite 3.9.0 -> 3.24.0 (source id c7ee0833225bfd8c5ec2f9bf62b97c4e04d03bd9566366d5221ac8fb199a87ca)\n'
