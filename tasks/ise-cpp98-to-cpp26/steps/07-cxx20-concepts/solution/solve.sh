#!/usr/bin/env bash
# Reference solution for step 7, "cxx20-concepts".
#
# Whole files, not a patch, for the reason step 1's solve.sh gives: a patch
# that fails to apply because an earlier hunk moved by a line is a solution
# that scores zero for a reason nobody can see. Every path manifest.txt
# names replaces its counterpart in /app outright, or -- for the one path
# this step introduces -- creates it. Step 7 removes no file, so deleted.txt
# is empty of paths and stays here only so that the check below has
# something to read. manifest.txt names the paths; tree/ holds them.
#
# The tree these files are differences *from* is steps 1 to 6's solutions
# applied in order -- what an oracle run has in /app when step 7 starts.
#
# Asserted, not assumed: every path the manifest names is checked to exist
# under tree/ before anything is copied, and every write is verified byte
# for byte afterwards.
# An oracle run that silently applied half of itself would report a plausible
# partial score, which is the one failure this file must not have.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP=${APP:-/app}
TREE="$HERE/tree"

[ -d "$TREE" ] || { echo "solve.sh: no tree/ beside me" >&2; exit 1; }
[ -d "$APP" ]  || { echo "solve.sh: $APP is not a directory" >&2; exit 1; }

# manifest.txt, not a listing of tree/, is what this step writes. Harbor's
# oracle agent uploads every step's solution/ into one container directory
# and does not clear it between steps, so the tree/ beside this script holds
# the union of steps 1..k -- well over a hundred paths by the time step 7
# runs, not 6. A listing therefore describes the run rather than the step.
# manifest.txt ships with the step and is overwritten by each upload, so it
# always describes the step whose solve.sh is executing. Measured, not
# assumed: job 01-cpp98-to-cpp26-step02-oracle, where the listing form
# aborted with "expected 21 files under tree/, found 69" and step 2 scored
# the floor.
mapfile -t WANT < "$HERE/manifest.txt"
echo "solve.sh: ${#WANT[@]} file(s) to write into $APP"
if [ "${#WANT[@]}" -ne 6 ]; then
    echo "solve.sh: expected 6 paths in manifest.txt, found ${#WANT[@]}" >&2
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
# Every file this step *rewrites* must already be there: step 7 edits the
# tree step 6 left, and a path that is missing means /app is not that tree.
# ise/main/ise_concepts.h is the exception and the only one -- this step
# introduces it, so its absence from /app is the expected state and its
# presence would mean /app is not step 6's tree either. Both directions are
# checked, because a NEW list that quietly went stale would turn this test
# into a test of nothing.
NEW=(ise/main/ise_concepts.h)
is_new() { local p; for p in "${NEW[@]}"; do [ "$p" = "$1" ] && return 0; done; return 1; }
missing=0
for rel in "${WANT[@]}"; do
    if is_new "$rel"; then
        [ -e "$APP/$rel" ] && {
            echo "ALREADY PRESENT, EXPECTED NEW: $rel" >&2
            missing=$((missing + 1)); }
    else
        [ -f "$APP/$rel" ] || { echo "ABSENT: $rel" >&2; missing=$((missing + 1)); }
    fi
done
if [ "$missing" -ne 0 ]; then
    echo "solve.sh: $missing path(s) are not in the state this step starts from" >&2
    exit 1
fi

for rel in "${WANT[@]}"; do
    mkdir -p "$APP/$(dirname "$rel")"
    cp -f "$TREE/$rel" "$APP/$rel"
done

removed=0
while IFS= read -r rel; do
    case "$rel" in ''|'#'*) continue ;; esac
    if [ -e "$APP/$rel" ]; then
        rm -f "$APP/$rel"
        removed=$((removed + 1))
    fi
    dir="$APP/$(dirname "$rel")"
    while [ "$dir" != "$APP" ] && [ -d "$dir" ] && [ -z "$(ls -A "$dir")" ]; do
        rmdir "$dir"
        dir="$(dirname "$dir")"
    done
done < "$HERE/deleted.txt"
echo "solve.sh: removed $removed legacy file(s)"

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
