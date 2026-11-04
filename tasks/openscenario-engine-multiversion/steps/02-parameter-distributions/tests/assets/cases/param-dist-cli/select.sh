#!/usr/bin/env bash
# --param_permutation picks ONE permutation out of the distribution instead of
# sweeping all of them, and an index the distribution does not contain is
# refused rather than clamped or ignored.
source /tests/caselib.sh

cp "$FIXTURES/param_cars.xosc" "$FIXTURES/straight_500m.xodr" \
   "$FIXTURES/dist_set6.xosc" "$WORK/"

initial_speed() {
    awk -F', ' -v who="$2" '$1 == "0.000" && $3 == who { print $10; exit }' "$1"
}
speed_is() {  # speed_is <csv> <entity> <expected> <what>
    local got
    got=$(initial_speed "$1" "$2")
    [ -n "$got" ] || fail "$4: no initial sample for $2 in $(basename "$1")"
    awk -v g="$got" -v e="$3" 'BEGIN { d = g - e; if (d < 0) d = -d; exit (d <= 0.01) ? 0 : 1 }' \
        || fail "$4: $2 started at $got, expected $3"
}

# Index 3 of 6 is the second EgoSpeed crossed with the first TargetSpeed.
run_esmini --osc param_cars.xosc --headless --fixed_timestep 0.05 \
           --param_dist dist_set6.xosc --param_permutation 3 --record one.dat
need_status 0 $? "esmini running a single selected permutation"
need_file "$WORK/one_4_of_6.dat"
[ -e "$WORK/one_1_of_6.dat" ] && fail "a selected permutation must not run the whole sweep"
[ -e "$WORK/one.dat" ] && fail "one.dat was written; a distribution run must distinguish its recordings"

run_tool "$DAT2CSV" "$WORK/one_4_of_6.dat"
need_status 0 $? "dat2csv on the selected permutation's recording"
need_file "$WORK/one_4_of_6.csv"
speed_is "$WORK/one_4_of_6.csv" Ego    24 "the selected permutation's EgoSpeed"
speed_is "$WORK/one_4_of_6.csv" Target  5 "the selected permutation's TargetSpeed"

# An index equal to the permutation count is one past the end.
run_esmini --osc param_cars.xosc --headless --fixed_timestep 0.05 \
           --param_dist dist_set6.xosc --param_permutation 6 --record oob.dat
[ "$?" -eq 0 ] && fail "esmini accepted a permutation index past the end of the distribution"
ls "$WORK"/oob*.dat > /dev/null 2>&1 && fail "a refused permutation index still ran the scenario"

# Asking for a permutation when no distribution was given is the same error.
run_esmini --osc param_cars.xosc --headless --fixed_timestep 0.05 \
           --param_permutation 2 --record nodist.dat
[ "$?" -eq 0 ] && fail "esmini accepted --param_permutation with no distribution loaded"
ls "$WORK"/nodist*.dat > /dev/null 2>&1 && fail "a permutation with no distribution still ran the scenario"

# Both options of req 3 must be DISCOVERABLE, not merely accepted. instruction.md's
# "Exact output that must not change" states that esmini's usage banner lists one
# option per line indented by exactly two spaces, and that the two options of §3
# and the flag of §4 JOIN that list rather than replacing it -- so a solution that
# parses --param_dist but never advertises it has not kept that clause. Only the
# option name and the documented two-space indent are matched: the descriptive
# text after the name is upstream wording that no document pins, and pinning it
# would score a string the candidate was never given.
#
# The banner's exit status is deliberately not checked -- instruction.md says so
# explicitly ("The banner's *exit status* is deliberately not part of this
# contract") because upstream returns non-zero here.
run_esmini --help
need_grep '^  --param_dist'        "$WORK/last.out" "--param_dist is listed in the usage banner"
need_grep '^  --param_permutation' "$WORK/last.out" "--param_permutation is listed in the usage banner"
# The inherited options are still there beside them: joining the list, not
# replacing it, is the half of the clause a bare presence check would miss.
need_grep '^Usage: esmini \[options\]' "$WORK/last.out" "the banner still opens with its inherited usage line"
need_grep '^  --osc <filename>'          "$WORK/last.out" "--osc is still listed beside the new options"
need_grep '^  --headless'                "$WORK/last.out" "--headless is still listed beside the new options"
exit 0
