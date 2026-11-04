#!/usr/bin/env bash
# make_inputs.sh <tests-dir> <out-dir>
#
# Copies this step's scored LAMMPS inputs out of the verifier's own build
# context into the workload directory. The inputs are never read from /app:
# the candidate controls /app, and a workload the candidate can edit is not a
# measurement.
set -eu
T=$1; O=$2
STEP=$(cat /etc/harbor-step-id)
mkdir -p "$O"
n=0
for f in "$T"/IN.*.step"$STEP"; do
    [ -e "$f" ] || continue
    base=$(basename "$f")
    name=${base#IN.}
    name=${name%.step"$STEP"}
    cp "$f" "$O/$name.in"
    n=$((n+1))
done
echo "generated $n inputs for step $STEP into $O"
[ "$n" -gt 0 ] || { echo "no inputs found for step $STEP"; exit 1; }
