#!/usr/bin/env bash
# Reference solution for step 5: the migrated tree, installed over /app.
#
# Replacing the tree wholesale rather than patching it, for the same reason as
# steps 1 to 4: a patch would encode the starting state, and the oracle has to
# produce the same result whether it runs after step 4's own reference solution
# or after whatever an agent left behind.  What is kept is exactly what this step
# does not own: the mock API server (the fixed half of the interface) and the
# environment's own timer.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The payload directory carries the step number rather than a plain `tree`,
# because harbor merges every step's solution/ into one /solution and never
# clears it between steps.  See step 2's solve.sh for the failure a shared name
# produces.
TREE="$HERE/tree-05"
[ -d "$TREE/src" ] || { echo "solution tree missing at $TREE" >&2; exit 1; }

fail() { echo "step 5 reference solution: $*" >&2; exit 1; }
must() { "$@" || fail "failed assertion: $*"; }

# The payload is checked before it is installed: this is the one place that would
# notice a /solution polluted by an earlier step.  Step 1's tree would restore
# src/actions/, step 3's would restore react 16 and the ReactDOM.render call, and
# step 4's - the nearest miss, and the one a wrong TREE above would produce -
# would restore an app.js whose twelve chart panels are static imports.
must test ! -e "$TREE/src/actions"
must test ! -e "$TREE/server"
must grep -q '"react": "\^18' "$TREE/package.json"
must grep -q '"react-dom": "\^18' "$TREE/package.json"
must grep -q 'createRoot' "$TREE/src/components/app.js"
must test "$(grep -c 'lazy(() => import(' "$TREE/src/components/app.js")" = 12
must grep -rq 'useDeferredValue' "$TREE/src/components/sellers/sellers.js"
must test "$(grep -rl 'useTransition' "$TREE/src" | wc -l)" = 3
must test "$(grep -rl '<Suspense fallback=' "$TREE/src" | wc -l)" = 3
# The toolbox registration is the sharpest tree-04/tree-05 discriminator there
# is: before this step two modules imported `echarts/lib/component/toolbox` and
# every other chart borrowed it from whichever of them the entry chunk happened
# to pull in.  Moving the chart bodies out of the entry chunk ends the loan, so
# the payload carries the registration on each module that draws a toolbox.
must test "$(grep -rl "echarts/lib/component/toolbox" "$TREE/src/components" | wc -l)" = 42
# `must` runs its arguments, so the absence of a line is asserted the other way
# round: the grep succeeding is the failure.
grep -rq "react-icons/lib" "$TREE/src" && fail "the payload still imports icons the version-2 way"
grep -q '"react-transition-group"' "$TREE/package.json" && fail "the payload still declares react-transition-group"

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
# oracle that silently copied step 4's tree back would be scored as a failed
# migration and read as a broken verifier.
must test -f /app/package.json
must test -f /app/package-lock.json
must test -f /app/vite.config.js
must test -f /app/index.html
must test -d /app/src/components
must test -d /app/public/lib
must test -d /app/server/demo1
must grep -q '"@reduxjs/toolkit"' /app/package.json
must grep -q '"react-router-dom"' /app/package.json
must grep -q "from 'react-dom/client'" /app/src/components/app.js
must grep -q '<StrictMode>' /app/src/components/app.js
must test "$(grep -rl 'ReactDOM.render' /app/src | wc -l)" = 0
must test "$(grep -rl 'react-icons/lib' /app/src | wc -l)" = 0
# The nine compare-by-seller screens whose cleanup used to dispose a chart that
# conditional initialisation may never have created.  StrictMode runs that
# cleanup on the first mount, so an unguarded one is nine console errors on every
# visit; the tenth file is guarded the same way for consistency.
must test "$(grep -rl 'Chart.current) {' /app/src/components/compare/sellers | wc -l)" = 10
# This step's own three constructs, each counted where it was put rather than
# anywhere in the tree: the deferred seller list, the three transitions around
# the range changes, and the twelve chart bodies behind React's `lazy` with the
# three boundaries that hold the shell in place while they arrive.
must test "$(grep -c 'useDeferredValue' /app/src/components/sellers/sellers.js)" = 2
must test "$(grep -rl 'useTransition' /app/src | wc -l)" = 3
must test "$(grep -c 'lazy(() => import(' /app/src/components/app.js)" = 12
must test "$(grep -rl '<Suspense fallback=' /app/src | wc -l)" = 3
must test "$(grep -rl "echarts/lib/component/toolbox" /app/src/components | wc -l)" = 42
must test "$(grep -rl "echarts/lib/component/grid" /app/src/components | wc -l)" = 34
must test "$(find /app/src -name '*.js' | wc -l)" = 75

echo "step 5 reference solution installed: $(find /app/src -type f | wc -l) source files"
