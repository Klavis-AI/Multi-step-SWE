#!/usr/bin/env bash
# odrplot samples the road geometry to track.csv. The output file name is
# hardcoded and does not match the name its own usage text suggests; both are
# stable across this step.
source /tests/caselib.sh

run_tool "$ODRPLOT" "$FIXTURES/straight_500m.xodr"
need_status 0 $? "odrplot"
need_file "$WORK/track.csv"
need_grep 'Created track.csv using stepsize 1\.00' "$WORK/last.out" "odrplot completion line"
need_grep '^To plot it, run EnvironmentSimulator/Applications/odrplot/xodr\.py track\.csv$' \
    "$WORK/last.out" "odrplot plotting hint line"

need_grep '^lane, 1, 0, [0-9-]+, (driving|no-driving)$' "$WORK/track.csv" "lane header rows"
need_grep '^-?[0-9]+\.[0-9]+, -?[0-9]+\.[0-9]+, -?[0-9]+\.[0-9]+, -?[0-9]+\.[0-9]+$' "$WORK/track.csv" "sample point rows"

# One lane header per lane per lane section, and a sample every metre of a
# 500 m road for each of them.
lanes=$(grep -c '^lane, ' "$WORK/track.csv")
[ "$lanes" -eq 7 ] || fail "expected 7 lane rows for straight_500m, got $lanes"
pts=$(grep -c '^-\?[0-9]' "$WORK/track.csv")
[ "$pts" -ge 3400 ] && [ "$pts" -le 3600 ] || fail "expected about 3507 sample rows, got $pts"

# A file that is not an OpenDRIVE network is an error.
printf 'not xodr\n' > "$WORK/bad.xodr"
run_tool "$ODRPLOT" "$WORK/bad.xodr"
[ "$?" -eq 0 ] && fail "odrplot reported success on a non-OpenDRIVE file"

# With no argument at all it prints its usage string and refuses. "ordplot" is
# misspelled upstream and the frozen requirement keeps the misspelling, so the
# assertion does too -- correcting it would be a change to the output.
run_tool "$ODRPLOT"
[ "$?" -eq 0 ] && fail "odrplot reported success with no input file"
need_grep '^Usage: ordplot openDriveFile\.xodr \[Output file, default=output\.csv\] \[Sampling_step, default=1\.0\]$' \
    "$WORK/last.out" "odrplot usage string"
exit 0
