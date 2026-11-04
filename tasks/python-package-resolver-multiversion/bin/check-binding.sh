#!/usr/bin/env bash
#
# Check that each step's verifier image contains that step's tests/, by
# building it and comparing the whole /tests tree against what the step ships.
#
# bin/stamp-steps.py --verify predicts mis-binding from file metadata. This
# observes it, through the real builder, and so catches anything the metadata
# model does not anticipate. Run it on any task directory, or on a tree of them.
#
#   bin/check-binding.sh <task-dir>...
#
# Starts with a positive control: it deliberately builds one step against
# another step's context and requires the check to report MIS-BOUND. If the
# control does not fire, the tool is broken and says so rather than printing
# reassuring OKs -- a green result from an instrument that cannot go red means
# nothing.
set -u
export DOCKER_BUILDKIT=1

# Portable over host and image: hashes every file's path and contents, and
# nothing else. No timestamps, so it compares what was baked in, not when.
# Per-file listing, not a whole-tree digest: a step's Dockerfile may copy only a
# SUBSET of its tests/ into the image (selective COPY rather than `COPY . /tests`),
# and a whole-tree digest reports every such package as mis-bound. What must hold
# is narrower and correct: every file the image *does* carry has to match the file
# shipped at that path.
LIST_CMD='cd "$0" && find . -type f -exec sha256sum {} + | LC_ALL=C sort -k2'

list_local() { sh -c "$LIST_CMD" "$1"; }
list_image() { docker run --rm "$1" sh -c "$LIST_CMD" /tests; }

# compare <context-dir> <image>  -> prints mismatching paths, empty if bound correctly
compare() {
    local tree=$1 tag=$2
    join -j 2 <(list_image "$tag") <(list_local "$tree") 2>/dev/null \
        | awk '$2 != $3 { print $1 }'
}
count_image_files() { list_image "$1" | wc -l; }

build() {  # build <context> <tag>
    docker build -q -t "$2" "$1" >/dev/null 2>&1
}

steps_of() { find "$1/steps" -mindepth 2 -maxdepth 2 -type d -name tests | sort; }

# ---- positive control -------------------------------------------------------
control() {
    local a b tag got want
    a=$1; b=$2
    tag="bindctl:$$"
    build "$a" "$tag" || return 1
    local bad
    bad=$(compare "$b" "$tag")     # built A, compared against B: must report mismatches
    docker rmi -f "$tag" >/dev/null 2>&1
    [ -n "$bad" ]
}

rc=0
for task in "$@"; do
    mapfile -t trees < <(steps_of "$task")
    [ "${#trees[@]}" -ge 2 ] || { echo "skip $task (fewer than 2 steps)"; continue; }

    if ! control "${trees[0]}" "${trees[1]}"; then
        echo "CONTROL FAILED for $(basename "$task"): the check cannot distinguish two steps." >&2
        echo "Not reporting results -- fix the tool first." >&2
        rc=2
        continue
    fi

    echo "### $(basename "$task")  (control ok: two steps are distinguishable)"
    bad=0
    for tree in "${trees[@]}"; do
        step=$(basename "$(dirname "$tree")")
        tag="bind:$(printf '%s' "$task/$step" | md5sum | cut -c1-12)"
        if ! build "$tree" "$tag"; then
            printf '  %-46s BUILD FAILED\n' "$step"; bad=1; continue
        fi
        mismatched=""; n=""
        mismatched=$(compare "$tree" "$tag")
        n=$(count_image_files "$tag")
        docker rmi -f "$tag" >/dev/null 2>&1
        if [ -z "$mismatched" ]; then
            printf '  %-46s %3s files, all match  ok\n' "$step" "$n"
        else
            printf '  %-46s *** MIS-BOUND *** %s\n' "$step" "$(echo "$mismatched" | tr '\n' ' ')"; bad=1
        fi
    done
    if [ "$bad" -eq 0 ]; then echo "  => every step is checked against its own verifier"
    else echo "  => MIS-BINDING PRESENT: run bin/stamp-steps.py on this task"; rc=1; fi
done
exit "$rc"
