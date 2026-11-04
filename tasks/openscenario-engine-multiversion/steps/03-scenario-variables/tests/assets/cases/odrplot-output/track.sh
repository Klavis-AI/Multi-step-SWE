#!/usr/bin/env bash
# odrplot samples the road geometry to track.csv. The output file name is
# hardcoded and does not match the name its own usage text suggests; both are
# stable across this step.
source /tests/caselib.sh

run_tool "$ODRPLOT" "$FIXTURES/straight_500m.xodr"
need_status 0 $? "odrplot"
need_file "$WORK/track.csv"
need_grep 'Created track.csv using stepsize 1\.00' "$WORK/last.out" "odrplot completion line"

need_grep '^lane, 1, 0, [0-9-]+, (driving|no-driving)$' "$WORK/track.csv" "lane header rows"
need_grep '^-?[0-9]+\.[0-9]+, -?[0-9]+\.[0-9]+, -?[0-9]+\.[0-9]+, -?[0-9]+\.[0-9]+$' "$WORK/track.csv" "sample point rows"

# One lane header per lane per lane section, and a sample every stepsize metre
# of a 500 m road for each of them. At the documented default stepsize of 1.00
# that is 501 samples per lane, not 500: the last one is the clamped sample at
# the very end of the road. 7 lanes x 501 = 3507, and an implementation that
# dropped the end-of-road clamp would produce exactly 3500, so the bound has to
# be exact to discriminate the two.
lanes=$(grep -c '^lane, ' "$WORK/track.csv")
[ "$lanes" -eq 7 ] || fail "expected 7 lane rows for straight_500m, got $lanes"
pts=$(grep -c '^-\?[0-9]' "$WORK/track.csv")
[ "$pts" -eq 3507 ] || fail "expected 3507 sample rows for straight_500m, got $pts"

# A file that is not an OpenDRIVE network is an error.
printf 'not xodr\n' > "$WORK/bad.xodr"
run_tool "$ODRPLOT" "$WORK/bad.xodr"
[ "$?" -eq 0 ] && fail "odrplot reported success on a non-OpenDRIVE file"
exit 0
