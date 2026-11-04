#!/usr/bin/env bash
# Reference solution for step 6: the migrated tree, installed over /app.
#
# Replacing the tree wholesale rather than patching it, for the same reason as
# steps 1 to 5: a patch would encode the starting state, and the oracle has to
# produce the same result whether it runs after step 5's own reference solution
# or after whatever an agent left behind.  What is kept is exactly what this step
# does not own: the mock API server (the fixed half of the interface) and the
# environment's own timer.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The payload directory carries the step number rather than a plain `tree`,
# because harbor merges every step's solution/ into one /solution and never
# clears it between steps.  See step 2's solve.sh for the failure a shared name
# produces.
TREE="$HERE/tree-06"
[ -d "$TREE/src" ] || { echo "solution tree missing at $TREE" >&2; exit 1; }

fail() { echo "step 6 reference solution: $*" >&2; exit 1; }
must() { "$@" || fail "failed assertion: $*"; }

# The payload is checked before it is installed: this is the one place that would
# notice a /solution polluted by an earlier step.  Step 1's tree would restore
# src/actions/, step 3's would restore react 16 and the ReactDOM.render call, and
# step 5's - the nearest miss, and the one a wrong TREE above would produce -
# would restore react 18, react-redux 7 and the three files that still declare
# their props through the prop-types package.
must test ! -e "$TREE/src/actions"
must test ! -e "$TREE/server"
must grep -q '"react": "\^19' "$TREE/package.json"
must grep -q '"react-dom": "\^19' "$TREE/package.json"
must grep -q '"react-redux": "\^9' "$TREE/package.json"
must test "$(grep -c 'lazy(() => import(' "$TREE/src/components/app.js")" = 12
# `must` runs its arguments, so the absence of a line is asserted the other way
# round: the grep succeeding is the failure.
grep -q '"prop-types"' "$TREE/package.json" && fail "the payload still declares prop-types"
grep -rq "from 'prop-types'" "$TREE/src" && fail "the payload still imports prop-types"
grep -rq "react-icons/lib" "$TREE/src" && fail "the payload still imports icons the version-2 way"

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
# oracle that silently copied step 5's tree back would be scored as a failed
# migration and read as a broken verifier.
must test -f /app/package.json
must test -f /app/package-lock.json
must test -f /app/vite.config.js
must test -f /app/index.html
must test -d /app/src/components
must test -d /app/public/lib
must test -d /app/server/demo1

# This step's own two constructs.  First the runtime and the one peer that stood
# in its way: react-redux 7 declares a peer range that stops at React 18, so a
# tree carrying both is a tree whose install cannot be reproduced from its
# lockfile.  The lockfile is asserted alongside the manifest, because a manifest
# that asks for 19 over a lockfile that pins 18 installs 18.
must grep -q '"react": "\^19' /app/package.json
must grep -q '"react-dom": "\^19' /app/package.json
must grep -q '"react-redux": "\^9' /app/package.json
must node -e 'const l=require("/app/package-lock.json").packages;
    const m = n => Number(l["node_modules/"+n].version.split(".")[0]);
    if (m("react") !== 19 || m("react-dom") !== 19 || m("react-redux") < 9) process.exit(1)'
# Second the validation layer.  Removed, not silenced: the package is gone from
# the manifest and from the lockfile, so nothing can reach it at build time even
# if a stray import survived.
must node -e 'const p=require("/app/package.json");
    if ((p.dependencies||{})["prop-types"] || (p.devDependencies||{})["prop-types"]) process.exit(1)'
must node -e 'if (require("/app/package-lock.json").packages["node_modules/prop-types"]) process.exit(1)'
must test "$(grep -rl "from 'prop-types'" /app/src | wc -l)" = 0
must test "$(grep -rl 'propTypes' /app/src | wc -l)" = 0
# Overriding the peer range instead of upgrading the bindings is the one shortcut
# that installs, builds and behaves.  The oracle must never be that tree.
must node -e 'const p=require("/app/package.json");
    if (p.overrides || p.resolutions) process.exit(1)'

# What the five steps before this one built, asserted where each of them put it.
must grep -q '"@reduxjs/toolkit"' /app/package.json
must grep -q '"react-router-dom"' /app/package.json
must grep -q "from 'react-dom/client'" /app/src/components/app.js
must grep -q '<StrictMode>' /app/src/components/app.js
must test "$(grep -rl 'ReactDOM.render' /app/src | wc -l)" = 0
must test "$(grep -rl 'react-icons/lib' /app/src | wc -l)" = 0
must test "$(grep -rl 'Chart.current) {' /app/src/components/compare/sellers | wc -l)" = 10
must test "$(grep -c 'useDeferredValue' /app/src/components/sellers/sellers.js)" = 2
must test "$(grep -rl 'useTransition' /app/src | wc -l)" = 3
must test "$(grep -c 'lazy(() => import(' /app/src/components/app.js)" = 12
must test "$(grep -rl '<Suspense fallback=' /app/src | wc -l)" = 3
must test "$(grep -rl "echarts/lib/component/toolbox" /app/src/components | wc -l)" = 42
must test "$(grep -rl "echarts/lib/component/grid" /app/src/components | wc -l)" = 34
must test "$(find /app/src -name '*.js' | wc -l)" = 75

echo "step 6 reference solution installed: $(find /app/src -type f | wc -l) source files"
