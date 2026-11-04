#!/usr/bin/env bash
# Reference solution for step 10, "cxx20-coroutines".
#
# Whole files, not a patch, for the reason step 1's solve.sh gives: a patch
# that fails to apply because an earlier hunk moved by a line is a solution
# that scores zero for a reason nobody can see. Every path manifest.txt
# names replaces its counterpart in /app outright.
#
# Step 10 adds two files -- the coroutine header and the interface unit that
# exports it -- so the NEW list is read from new.txt rather than written
# into this file, because a list kept by hand beside a generated manifest is
# a list that goes stale silently.
#
# Nothing is removed, and that is worth saying plainly because the milestone
# deletes a state machine: two enumerations, a struct and a typedef all go
# from ise_http.h. They go from inside files that stay, and no file in this
# tree existed only to hold them. make_step10_solution.sh refuses to write a
# manifest that would make the empty deleted.txt a lie.
#
# manifest.txt and new.txt are not hand-written:
# authoring/make_step10_solution.sh computes them as exactly the set of
# paths that differ between step 9's gold tree and step 10's, and the subset
# of those the floor does not have.
#
# The tree these files are differences *from* is steps 1 to 9's solutions
# applied in order -- what an oracle run has in /app when step 10 starts.
#
# Asserted, not assumed: every path the manifest names is checked to exist
# under tree/ before anything is copied, every new path is checked to be
# absent from /app and every other path to be present, and every write is
# verified byte for byte afterwards. An oracle run that silently applied
# half of itself would report a plausible partial score, which is the one
# failure this file must not have.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP=${APP:-/app}
TREE="$HERE/tree"

[ -d "$TREE" ] || { echo "solve.sh: no tree/ beside me" >&2; exit 1; }
[ -d "$APP" ]  || { echo "solve.sh: $APP is not a directory" >&2; exit 1; }

# manifest.txt, not a listing of tree/, is what this step writes. Harbor's
# oracle agent uploads every step's solution/ into one container directory
# and does not clear it between steps, so the tree/ beside this script holds
# the union of steps 1..k. A listing therefore describes the run rather than
# the step. Measured, not assumed: job 01-cpp98-to-cpp26-step02-oracle,
# where the listing form aborted with "expected 21 files under tree/, found
# 69" and step 2 scored the floor.
mapfile -t WANT < "$HERE/manifest.txt"
mapfile -t NEW < "$HERE/new.txt"
echo "solve.sh: ${#WANT[@]} file(s) to write into $APP, ${#NEW[@]} of them new"
if [ "${#WANT[@]}" -ne 41 ]; then
    echo "solve.sh: expected 41 paths in manifest.txt, found ${#WANT[@]}" >&2
    exit 1
fi
if [ "${#NEW[@]}" -ne 2 ]; then
    echo "solve.sh: expected 2 paths in new.txt, found ${#NEW[@]}" >&2
    exit 1
fi

is_new() { local p; for p in "${NEW[@]}"; do [ "$p" = "$1" ] && return 0; done; return 1; }

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

# The state this step starts from, checked in both directions: a path the
# manifest rewrites has to be there already, and a path it creates has to
# not be. Both halves matter -- a missing rewrite target means /app is not
# step 9's tree, and a NEW path that already exists means the list went
# stale against a tree that grew one.
wrong=0
for rel in "${WANT[@]}"; do
    if is_new "$rel"; then
        [ -e "$APP/$rel" ] && { echo "ALREADY PRESENT, EXPECTED NEW: $rel" >&2; wrong=$((wrong + 1)); }
    else
        [ -f "$APP/$rel" ] || { echo "ABSENT: $rel" >&2; wrong=$((wrong + 1)); }
    fi
done
if [ "$wrong" -ne 0 ]; then
    echo "solve.sh: $wrong path(s) are not in the state this step starts from" >&2
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
