#!/usr/bin/env bash
# Reference solution for step 3: the migrated tree, installed over /app.
#
# Replacing the tree wholesale rather than patching it, for the same reason as
# steps 1 and 2: a patch would encode the starting state, and the oracle has to
# produce the same result whether it runs after step 2's own reference solution
# or after whatever an agent left behind.  What is kept is exactly what this step
# does not own: the mock API server (the fixed half of the interface) and the
# environment's own timer.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The payload directory carries the step number rather than a plain `tree`,
# because harbor merges every step's solution/ into one /solution and never
# clears it between steps.  See step 2's solve.sh for the failure a shared name
# produces.
TREE="$HERE/tree-03"
[ -d "$TREE/src" ] || { echo "solution tree missing at $TREE" >&2; exit 1; }

fail() { echo "step 3 reference solution: $*" >&2; exit 1; }
must() { "$@" || fail "failed assertion: $*"; }

# The payload is checked before it is installed: this is the one place that would
# notice a /solution polluted by an earlier step.  Step 1's tree would restore
# src/actions/, step 2's would restore the v3 route table.
must test ! -e "$TREE/src/actions"
must test ! -e "$TREE/src/constants/dispatchType.js"
must test ! -e "$TREE/server"
must grep -q '"react-router-dom"' "$TREE/package.json"
# `must` runs its arguments, so the absence of a line is asserted the other way
# round: the grep succeeding is the failure.
grep -q '"react-router":' "$TREE/package.json" && fail "the payload still declares react-router 3"

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
# step is about rather than only the things the steps before it were about: an
# oracle that silently copied step 2's tree back would be scored as a failed
# migration and read as a broken verifier.
must test -f /app/package.json
must test -f /app/vite.config.js
must test -f /app/index.html
must test -d /app/src/components
must test -d /app/public/lib
must test -d /app/server/demo1
must test -f /app/src/store/api.js
must test ! -e /app/src/actions
must grep -q '"@reduxjs/toolkit"' /app/package.json
must grep -q '"react-router-dom"' /app/package.json
must grep -q 'createHashRouter' /app/src/components/app.js
must test "$(grep -rl "from 'react-router'" /app/src | wc -l)" = 0
must test "$(find /app/src -name '*.js' | wc -l)" = 75

echo "step 3 reference solution installed: $(find /app/src -type f | wc -l) source files"
