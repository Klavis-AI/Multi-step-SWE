#!/usr/bin/env bash
# Reference solution for step 6, "cxx17-compile-time".
#
# Whole files, not a patch, for the reason step 1's solve.sh gives: a patch
# that fails to apply because an earlier hunk moved by a line is a solution
# that scores zero for a reason nobody can see. Every path manifest.txt
# names replaces its counterpart in /app outright. Step 6 adds no file and
# removes none, so deleted.txt is empty and stays here only so that the
# check below has something to read. manifest.txt names the paths; tree/
# holds them.
#
# The tree these files are differences *from* is steps 1 to 5's solutions
# applied in order -- what an oracle run has in /app when step 6 starts.
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
# the union of steps 1..k -- a hundred-odd paths by the time step 6 runs, not
# 6. A listing therefore describes the run rather than the step.
# manifest.txt
# ships with the step and is overwritten by each upload, so it always
# describes the step whose solve.sh is executing. Measured, not assumed: job
# 01-cpp98-to-cpp26-step02-oracle, where the listing form aborted with
# "expected 21 files under tree/, found 69" and step 2 scored the floor. The
# same mistake here would cost more: step 6 rewrites both halves of
# ise_sys_utils and the four class templates of requirement 6, and a step
# that wrote none of it would still build and still pass the behaviour suite.
mapfile -t WANT < "$HERE/manifest.txt"
echo "solve.sh: ${#WANT[@]} file(s) to write into $APP"
if [ "${#WANT[@]}" -ne 9 ]; then
    echo "solve.sh: expected 9 paths in manifest.txt, found ${#WANT[@]}" >&2
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
# Every file this step rewrites must already be there: step 6 edits the tree
# step 5 left, and a path that is missing means /app is not that tree.
missing=0
for rel in "${WANT[@]}"; do
    [ -f "$APP/$rel" ] || { echo "ABSENT: $rel" >&2; missing=$((missing + 1)); }
done
if [ "$missing" -ne 0 ]; then
    echo "solve.sh: $missing path(s) this step rewrites are not in $APP" >&2
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
