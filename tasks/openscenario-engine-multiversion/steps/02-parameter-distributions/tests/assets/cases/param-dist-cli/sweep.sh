#!/usr/bin/env bash
# --param_dist on its own runs the WHOLE distribution: one execution of the
# scenario per permutation, from one invocation of the player, with every
# per-permutation output file distinguished from the others.
#
# The recordings are the evidence, not the log text: six files have to appear
# where one was asked for, the plain name must NOT appear, and decoding the
# first and the last shows two different pairs of initial speeds.
source /tests/caselib.sh

cp "$FIXTURES/param_cars.xosc" "$FIXTURES/straight_500m.xodr" \
   "$FIXTURES/dist_set6.xosc" "$WORK/"

# initial_speed <csv> <entity-name>
#   The first recorded sample's speed for one entity: the t=0.000 row whose
#   name column holds that entity, field 10.
#
#   The field number is a fixed offset, deliberately. dat2csv's column header
#   is itself frozen by instruction.md's "Exact output that must not change"
#   (`time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot`) and is
#   separately asserted verbatim in the se-lib-additions and esmini-cli-headless
#   groups, so `speed` is the tenth field by contract rather than by luck. The
#   header cannot be used to look the position up here anyway: it is the second
#   line of the file, not the first, and caselib's csv_column helper reads
#   NR==1 only.
initial_speed() {
    awk -F', ' -v who="$2" '$1 == "0.000" && $3 == who { print $10; exit }' "$1"
}

run_esmini --osc param_cars.xosc --headless --fixed_timestep 0.05 \
           --param_dist dist_set6.xosc --record rec.dat
need_status 0 $? "esmini sweeping a six-permutation distribution"

for i in 1 2 3 4 5 6; do
    need_file "$WORK/rec_${i}_of_6.dat"
done
# The undecorated name is what a player that ignored the distribution would
# write, so its absence is half the assertion.
[ -e "$WORK/rec.dat" ] && fail "rec.dat was written; a distribution run must distinguish its recordings"

# The permutations really are different runs, not six copies of one run.
# EgoSpeed sweeps {12, 24} and TargetSpeed sweeps {5, 10, 15}; the first
# permutation takes the first value of each and the last takes the last.
run_tool "$DAT2CSV" "$WORK/rec_1_of_6.dat"
need_status 0 $? "dat2csv on the first permutation's recording"
need_file "$WORK/rec_1_of_6.csv"
run_tool "$DAT2CSV" "$WORK/rec_6_of_6.dat"
need_status 0 $? "dat2csv on the last permutation's recording"
need_file "$WORK/rec_6_of_6.csv"

for spec in "rec_1_of_6.csv Ego 12" "rec_1_of_6.csv Target 5" \
            "rec_6_of_6.csv Ego 24" "rec_6_of_6.csv Target 15"; do
    set -- $spec
    got=$(initial_speed "$WORK/$1" "$2")
    [ -n "$got" ] || fail "$1: no initial sample for $2"
    awk -v g="$got" -v e="$3" 'BEGIN { d = g - e; if (d < 0) d = -d; exit (d <= 0.01) ? 0 : 1 }' \
        || fail "$1: $2 started at $got, expected $3 for this permutation"
done
