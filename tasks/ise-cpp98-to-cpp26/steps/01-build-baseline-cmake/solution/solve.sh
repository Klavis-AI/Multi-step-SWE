#!/usr/bin/env bash
# Reference solution for step 1, "build-baseline-cmake".
#
# Whole files, not a patch. The step rewrites 50 of the pinned tree's files
# and adds one, and a patch that fails to apply because an earlier hunk moved
# by a line is a solution that scores zero for a reason nobody can see. Every
# path manifest.txt names replaces its counterpart in /app outright; every
# path in deleted.txt is removed from /app.
#
# Asserted, not assumed: every path the manifest names is checked to exist
# under tree/ before anything is copied, and every write is verified byte
# for byte afterwards. An oracle run that silently applied half of itself
# would report a plausible partial score, which is the one failure this
# file must not have.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP=${APP:-/app}
TREE="$HERE/tree"

[ -d "$TREE" ] || { echo "solve.sh: no tree/ beside me" >&2; exit 1; }
[ -d "$APP" ]  || { echo "solve.sh: $APP is not a directory" >&2; exit 1; }

# manifest.txt, not a listing of tree/, is what this step writes. Harbor's
# oracle agent uploads every step's solution/ into one container directory
# and does not clear it between steps, so from step 2 on the tree/ beside a
# solve.sh holds the union of the steps before it. Step 1 is uploaded first
# and into an empty directory, so here the two forms agree; it reads the
# manifest anyway, because "correct because it happens to run first" is not
# a property a later step can inherit.
mapfile -t WANT < "$HERE/manifest.txt"
echo "solve.sh: ${#WANT[@]} file(s) to write into $APP"
if [ "${#WANT[@]}" -ne 53 ]; then
    echo "solve.sh: expected 53 paths in manifest.txt, found ${#WANT[@]}" >&2
    exit 1
fi
# Path by path as well as by count: a manifest naming a file tree/ does not
# have is a solution that would copy nothing and report that it wrote it.
absent=0
for rel in "${WANT[@]}"; do
    if [ -z "$rel" ] || [ ! -f "$TREE/$rel" ]; then
        echo "NOT IN TREE: $rel" >&2
        absent=$((absent + 1))
    fi
done
if [ "$absent" -ne 0 ]; then
    echo "solve.sh: $absent manifest path(s) are not under tree/" >&2
    exit 1
fi

for rel in "${WANT[@]}"; do
    mkdir -p "$APP/$(dirname "$rel")"
    cp -f "$TREE/$rel" "$APP/$rel"
done

# The legacy build, removed rather than left beside the new one.
removed=0
while IFS= read -r rel; do
    case "$rel" in ''|'#'*) continue ;; esac
    if [ -e "$APP/$rel" ]; then
        rm -f "$APP/$rel"
        removed=$((removed + 1))
    fi
    # Walk up, removing whatever the removal emptied, and stop at $APP.
    dir="$APP/$(dirname "$rel")"
    while [ "$dir" != "$APP" ] && [ -d "$dir" ] && [ -z "$(ls -A "$dir")" ]; do
        rmdir "$dir"
        dir="$(dirname "$dir")"
    done
done < "$HERE/deleted.txt"
echo "solve.sh: removed $removed legacy build file(s)"

# Every write landed, byte for byte. cmp rather than a count: a copy that
# wrote a truncated file still increments a count.
bad=0
for rel in "${WANT[@]}"; do
    cmp -s "$TREE/$rel" "$APP/$rel" || { echo "MISMATCH: $rel" >&2; bad=$((bad + 1)); }
done
while IFS= read -r rel; do
    case "$rel" in ''|'#'*) continue ;; esac
    [ -e "$APP/$rel" ] && { echo "STILL PRESENT: $rel" >&2; bad=$((bad + 1)); }
done < "$HERE/deleted.txt"
if [ "$bad" -ne 0 ]; then
    echo "solve.sh: $bad path(s) are not what they should be" >&2
    exit 1
fi

echo "solve.sh: ${#WANT[@]} written, $removed removed, all verified"
