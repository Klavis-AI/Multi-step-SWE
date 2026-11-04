#!/usr/bin/env bash
# rowtime.sh <lmp-binary> <input-file> [row] [reps]
#
# Times one section of a LAMMPS run twice -- once on the reference styles and
# once with LAMMPS's style suffix turned on -- and prints the ratio. This is the
# same instrument the measurement uses, on an input you choose. The measurement
# input is generated at a size and stepping that are not disclosed; optimise the
# general case, not one configuration.
#
# What it does, exactly:
#
#   * runs <input-file> with no extra flags, and then with
#     `-pk intel 0 omp 0 mode double -sf intel`, which is what makes LAMMPS
#     resolve each style the input names as "<style>/intel", falling back to
#     "<style>" when the accelerated twin is absent. So an input that names
#     `pair_style lj/cut 2.5` needs no editing: the suffix does the switching;
#   * reads LAMMPS's own "MPI task timing breakdown" row named by [row]
#     (Pair, Kspace, Neigh, ...) -- that row is the narrow timer around that
#     family of kernels, so nothing else in the timestep dilutes it. The row
#     comes from the **log file**, because `-screen none` suppresses the table
#     on stdout;
#   * an input with more than one `run` command prints one timing table per
#     `run`, and the measurement reads the **last** table in the log -- the final
#     run, not the warm-up. This script reads the same one;
#   * repeats [reps] times (default 3), **alternating** the two configurations
#     one repetition at a time, and reduces each side with the **median** of its
#     repetitions -- also what the measurement does. This host is shared with other
#     tenants and the accelerated kernel is the more sensitive side, so
#     whichever side happens to land in a quiet window must not set the ratio;
#   * pins to one core with `taskset`, so both sides see the same core, and
#     runs single-threaded (OMP_NUM_THREADS=1).
#
# The measurement's ratio is this ratio, with one guard:
#
#       speedup = min(your reference time, the pristine reference tree's time)
#                 / your accelerated time
#
# If you leave the reference implementation alone -- which you should -- the
# first two are the same number and this script's output is the measurement's.
# The min() is the one part of the measurement's arithmetic this script cannot
# reproduce, because the pristine tree is not in this container; it exists so
# that slowing the reference down buys nothing.
#
# Usage with the benchmark inputs that ship in the source tree, from
# /app/lammps/src/INTEL/TEST:
#
#   export LMP_ROOT=/app/lammps
#   /app/tools/rowtime.sh /app/build/lmp in.intel.lj Pair
#
# LMP_ROOT is read by those inputs themselves (they do `getenv LMP_ROOT`); the
# script also passes the tree root as `-var ROOT`, which is what the generated
# measurement inputs use, so both kinds work.
set -u

LMP=${1:?usage: rowtime.sh <lmp> <input> [row] [reps]}
INPUT=${2:?usage: rowtime.sh <lmp> <input> [row] [reps]}
ROW=${3:-Pair}
REPS=${4:-3}
CORE=${ROWTIME_CORE:-0}
TREE=${LMP_ROOT:-/app/lammps}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

run_one() {  # <cfg> <0|1> <rep>
    local cfg=$1 sfx=$2 r=$3
    local -a extra=()
    [ "$sfx" = 1 ] && extra=(-pk intel 0 omp 0 mode double -sf intel)
    OMP_NUM_THREADS=1 taskset -c "$CORE" "$LMP" -in "$INPUT" \
        -log "$tmp/log.$cfg.$r" -screen none -var ROOT "$TREE" "${extra[@]}" \
        > "$tmp/out.$cfg.$r" 2>&1
    if ! grep -q 'Loop time of' "$tmp/log.$cfg.$r" 2>/dev/null; then
        echo "rowtime: the $cfg run failed (rep $r). Last lines:" >&2
        tail -4 "$tmp/out.$cfg.$r" >&2
        return 1
    fi
}

# The row's time from the LAST timing table of one log -- the reduction the
# measurement's row_of() applies. A log with a warm-up run followed by the final
# run has two tables; reading the minimum over both would favor the warm-up.
row_last() {  # <cfg> <row>
    local cfg=$1 row=$2 f
    for f in "$tmp"/log."$cfg".*; do
        [ -f "$f" ] || continue
        awk -v r="$row" '$1==r && $2=="|" {v=$3} END{if (v!="") print v}' "$f"
    done
}

# The median of those, over the repetitions -- the measurement's rows_of_median().
rows_median() {  # <cfg> <row>
    row_last "$1" "$2" | sort -g | awk '{a[NR]=$1} END{if(NR)print a[int((NR+1)/2)]}'
}

# One repetition at a time, alternating the two configurations, so both sides
# sample the same contention window.
for r in $(seq 1 "$REPS"); do
    run_one base 0 "$r" || exit 1
    run_one intel 1 "$r" || exit 1
done

b=$(rows_median base "$ROW")
i=$(rows_median intel "$ROW")
if [ -z "$b" ]; then
    echo "rowtime: no '$ROW' row in the reference output; the available rows are:" >&2
    awk '$2=="|" {print "  "$1}' "$tmp/log.base.1" >&2
    exit 1
fi
if [ -z "$i" ]; then
    # A missing accelerated row means the run died rather than that the style is
    # slow. Say so: a silent 0 here would read as "infinitely fast" in the
    # inverse, and as "no data" only if you happen to notice.
    echo "rowtime: no '$ROW' row in the accelerated output -- the run did not" >&2
    echo "         produce a timing table. Do not read this as a result." >&2
    tail -4 "$tmp/out.intel.1" >&2
    exit 1
fi
awk -v row="$ROW" -v a="$b" -v c="$i" -v n="$REPS" 'BEGIN{
    printf "%s: reference %s s   accelerated %s s   ratio %.4f  (median of %d runs each)\n",
           row, a, c, a/c, n
}'
