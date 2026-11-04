#!/usr/bin/env bash
# Reference solution for step 1: the migrated tree, installed over /app.
#
# Replacing the tree wholesale rather than patching it is deliberate.  A patch
# would encode the starting state, and the oracle has to produce the same result
# whether it runs on the pinned baseline or on a tree an earlier run left behind.
# What is kept is exactly what this step does not own: the mock API server (the
# fixed half of the interface) and the environment's own timer.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The payload directory carries the step number rather than a plain `tree`.
# Harbor uploads a step's solution/ into the container's /solution with a
# merging copy and never clears /solution between steps, so at step k the
# directory still holds everything steps 1..k-1 put there.  Were every step's
# payload called `tree`, step k would install the union of all the trees so far
# and so restore files it is supposed to have deleted.  A step-unique name gives
# each upload its own directory.
TREE="$HERE/tree-01"
[ -d "$TREE/src" ] || { echo "solution tree missing at $TREE" >&2; exit 1; }

# Assertions announce themselves.  Harbor sends both streams of this script to
# agent/oracle.txt and records only the exit code, so a bare `test` that fails
# leaves an empty log and nothing to diagnose.
fail() { echo "step 1 reference solution: $*" >&2; exit 1; }
must() { "$@" || fail "failed assertion: $*"; }

cd /app

# Everything the step replaces goes first, so a file the migration deletes -
# karma.conf.js, the webpack configs, lib/, test/, the 副本 duplicates - cannot
# survive by not being overwritten.
shopt -s dotglob
for entry in *; do
    case "$entry" in
        server|timer.sh) continue ;;
    esac
    rm -rf -- "$entry"
done
shopt -u dotglob

cp -a "$TREE/." /app/

# Assert the tree arrived, rather than trusting cp: an oracle that silently
# copied nothing would be scored as a failed migration and read as a broken
# verifier.
must test -f /app/package.json
must test -f /app/vite.config.js
must test -f /app/index.html
must test -d /app/src/components
must test -d /app/public/lib
must test -d /app/server/demo1
must test ! -e /app/karma.conf.js
must test ! -e /app/webpack.config.js
must test ! -e /app/test
must test "$(find /app/src -name '*.js' | wc -l)" = 77

echo "step 1 reference solution installed: $(find /app/src -type f | wc -l) source files"
