#!/usr/bin/env bash
# Reference solution for step 2: the migrated tree, installed over /app.
#
# Replacing the tree wholesale rather than patching it, for the same reason as
# step 1: a patch would encode the starting state, and the oracle has to produce
# the same result whether it runs after step 1's own reference solution or after
# whatever an agent left behind.  What is kept is exactly what this step does not
# own: the mock API server (the fixed half of the interface) and the
# environment's own timer.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The payload directory carries the step number rather than a plain `tree`.
# Harbor uploads a step's solution/ into the container's /solution with a
# merging copy and never clears /solution between steps, so at step k the
# directory still holds everything steps 1..k-1 put there.  Were every step's
# payload called `tree`, this step would install step 1's tree merged with its
# own and so restore src/actions/ and src/constants/dispatchType.js - the very
# files it exists to delete.  A step-unique name gives each upload its own
# directory.
TREE="$HERE/tree-02"
[ -d "$TREE/src" ] || { echo "solution tree missing at $TREE" >&2; exit 1; }

# Assertions announce themselves.  Harbor sends both streams of this script to
# agent/oracle.txt and records only the exit code, so a bare `test` that fails
# leaves an empty log and nothing to diagnose.
fail() { echo "step 2 reference solution: $*" >&2; exit 1; }
must() { "$@" || fail "failed assertion: $*"; }

# The payload is checked before it is installed, not only after: this is the one
# place that would notice a /solution polluted by an earlier step.
must test ! -e "$TREE/src/actions"
must test ! -e "$TREE/src/constants/dispatchType.js"
must test ! -e "$TREE/server"

cd /app

shopt -s dotglob
for entry in *; do
    case "$entry" in
        server|timer.sh) continue ;;
    esac
    rm -rf -- "$entry"
done
shopt -u dotglob

cp -a "$TREE/." /app/

# Assert the tree arrived, rather than trusting cp, and assert the things this
# step is about rather than only the things step 1 was about: an oracle that
# silently copied step 1's tree back would be scored as a failed migration and
# read as a broken verifier.
must test -f /app/package.json
must test -f /app/vite.config.js
must test -f /app/index.html
must test -d /app/src/components
must test -d /app/public/lib
must test -d /app/server/demo1
must test -f /app/src/store/api.js
must test -f /app/src/store/store.js
must test ! -e /app/src/actions
must test ! -e /app/src/constants/dispatchType.js
must grep -q '"@reduxjs/toolkit"' /app/package.json
must grep -q 'createApi' /app/src/store/api.js
must test "$(find /app/src -name '*.js' | wc -l)" = 74

echo "step 2 reference solution installed: $(find /app/src -type f | wc -l) source files"
