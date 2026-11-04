#!/usr/bin/env bash
# Reference solution for step 7: the migrated tree, installed over /app.
#
# Replacing the tree wholesale rather than patching it, for the same reason as
# steps 1 to 6: a patch would encode the starting state, and the oracle has to
# produce the same result whether it runs after step 6's own reference solution
# or after whatever an agent left behind.  What is kept is exactly what this step
# does not own: the mock API server (the fixed half of the interface) and the
# environment's own timer.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The payload directory carries the step number rather than a plain `tree`,
# because harbor merges every step's solution/ into one /solution and never
# clears it between steps.  See step 2's solve.sh for the failure a shared name
# produces.
TREE="$HERE/tree-07"
[ -d "$TREE/src" ] || { echo "solution tree missing at $TREE" >&2; exit 1; }

fail() { echo "step 7 reference solution: $*" >&2; exit 1; }
must() { "$@" || fail "failed assertion: $*"; }

# The payload is checked before it is installed: this is the one place that would
# notice a /solution polluted by an earlier step.  Step 6's tree is the nearest
# miss and the one a wrong TREE above would produce - it is on React 19 with the
# validation layer already gone, so the runtime assertions below would not catch
# it.  What separates it is this step's own two constructs: the compiler is not
# in its manifest and its components still memoise by hand.
must grep -q '"babel-plugin-react-compiler"' "$TREE/package.json"
must grep -q 'babel-plugin-react-compiler' "$TREE/vite.config.js"
must test ! -e "$TREE/src/actions"
must test ! -e "$TREE/server"
grep -rqE '\buseMemo\(|\buseCallback\(' "$TREE/src" && fail "the payload still memoises by hand"
grep -rq "from 'prop-types'" "$TREE/src" && fail "the payload still imports prop-types"

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
# oracle that silently copied step 6's tree back would be scored as a failed
# migration and read as a broken verifier.
must test -f /app/package.json
must test -f /app/package-lock.json
must test -f /app/vite.config.js
must test -f /app/index.html
must test -d /app/src/components
must test -d /app/public/lib
must test -d /app/server/demo1

# This step's own two constructs.  First the compiler: declared, locked, and
# actually wired into the build rather than merely installed.  The manifest and
# the lockfile are asserted together for the same reason step 6 asserted them
# together - a manifest that asks for a package over a lockfile that does not
# carry it installs nothing under `npm ci`.
must node -e 'const p=require("/app/package.json");
    const d={...(p.dependencies||{}),...(p.devDependencies||{})};
    if (!d["babel-plugin-react-compiler"]) process.exit(1)'
must node -e 'if (!require("/app/package-lock.json").packages["node_modules/babel-plugin-react-compiler"]) process.exit(1)'
must grep -q 'babel-plugin-react-compiler' /app/vite.config.js
# No module opts itself out of the compiler behind a directive, and nothing
# gates compilation per caller: either would make the build only partly
# compiled while every configuration reading still said it was on.
must test "$(grep -rl 'use no memo' /app/src | wc -l)" = 0
must test "$(grep -rl 'use memo if' /app/src | wc -l)" = 0
# Second the memoisation the compiler replaces.  Gone, not renamed: an import of
# the hook that survives with no call site is the shape a half-done removal
# leaves behind.
must test "$(grep -rlE '\buseMemo\(|\buseCallback\(' /app/src | wc -l)" = 0
must test "$(grep -rlE '\bmemo\(|React\.memo\b' /app/src | wc -l)" = 0
must test "$(grep -rlE 'useMemo|useCallback' /app/src | wc -l)" = 0
# The runtime the step before this one installed is untouched, including the
# store bindings, whose major this step has no reason to move.
must grep -q '"react": "\^19' /app/package.json
must grep -q '"react-dom": "\^19' /app/package.json
must grep -q '"react-redux": "\^9' /app/package.json
must node -e 'const l=require("/app/package-lock.json").packages;
    const v = n => l["node_modules/"+n].version.split(".").map(Number);
    const [rM]=v("react"), [dM]=v("react-dom"), [bM,bm]=v("react-redux");
    if (rM !== 19 || dM !== 19 || bM !== 9 || bm < 2) process.exit(1)'
must node -e 'const p=require("/app/package.json");
    if (p.overrides || p.resolutions) process.exit(1)'

# What the six steps before this one built, asserted where each of them put it.
must grep -q '"@reduxjs/toolkit"' /app/package.json
must grep -q '"react-router-dom"' /app/package.json
must test "$(grep -rl 'propTypes' /app/src | wc -l)" = 0
must node -e 'const p=require("/app/package.json");
    if ((p.dependencies||{})["prop-types"] || (p.devDependencies||{})["prop-types"]) process.exit(1)'
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

echo "step 7 reference solution installed: $(find /app/src -type f | wc -l) source files"
