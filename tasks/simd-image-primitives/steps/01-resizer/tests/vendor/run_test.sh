#!/usr/bin/env bash
# run_test.sh <Test binary> <W> <H> <C> <min_ms> <ot-path> [<group> ...]
#
# Each trailing argument is a test-group name; the framework spells its include
# filter as -fi=<group>, and this wrapper adds the prefix so the per-step data
# files can hold bare group names.
#
# One invocation of the project's own auto-check mode, with this task's
# measurement hygiene applied in one place: one test thread, one worker thread,
# the project's own cpu pinning, a pinned core set, and a fixed image geometry.
# Everything else about the run -- which groups, how many repetitions -- comes
# from the arguments, so the graded run and the guard run differ only in data.
set -u
bin="$1"; W="$2"; H="$3"; C="$4"; MT="$5"; ot="$6"; shift 6
filters=()
for g in "$@"; do filters+=("-fi=$g"); done
exec taskset -c 0,1 "$bin" -m=a -tt=1 -wt=1 -r=/tmp/cand \
    -w="$W" -h="$H" -c="$C" -mt="$MT" -ot="$ot" "${filters[@]}"
