#!/usr/bin/env bash
# Gate (proof of life). odrplot samples an OpenDRIVE network through the road-manager
# library and writes track.csv into the current directory. The file name, the
# completion line, the lane header rows and the number of sample points are
# unchanged across this step.
#
# Only the artefact is asserted, not the tool's console banner: what odrplot
# prints about the build and about unsupported projection attributes differs
# between the two ends of this step and is deliberately left unasserted.
source /tests/caselib.sh

run_tool "$ODRPLOT" "$FIXTURES/straight_500m.xodr"
need_status 0 $? "odrplot on straight_500m"
need_file "$WORK/track.csv"
need_grep 'Created track\.csv using stepsize 1\.00' "$WORK/last.out" "completion line"

need_grep '^lane, 1, 0, -1, driving$' "$WORK/track.csv" "right-hand driving lane header"
need_grep '^lane, 1, 0, 1, driving$' "$WORK/track.csv" "left-hand driving lane header"
need_grep '^-?[0-9]+\.[0-9]+, -?[0-9]+\.[0-9]+, -?[0-9]+\.[0-9]+, -?[0-9]+\.[0-9]+$' \
    "$WORK/track.csv" "sample point rows"

lanes=$(grep -c '^lane, ' "$WORK/track.csv")
[ "$lanes" -eq 7 ] || fail "expected 7 lane rows for straight_500m, got $lanes"
pts=$(grep -c '^-\?[0-9]' "$WORK/track.csv")
[ "$pts" -ge 3400 ] && [ "$pts" -le 3600 ] || fail "expected about 3507 sample rows, got $pts"

# A longer road with a different lane layout gives proportionally more points,
# so the sampling step really is being applied to the geometry.
rm -f "$WORK/track.csv"
run_tool "$ODRPLOT" "$FIXTURES/tri_lane_600m.xodr"
need_status 0 $? "odrplot on tri_lane_600m"
need_file "$WORK/track.csv"
lanes=$(grep -c '^lane, ' "$WORK/track.csv")
[ "$lanes" -eq 7 ] || fail "expected 7 lane rows for tri_lane_600m, got $lanes"
pts=$(grep -c '^-\?[0-9]' "$WORK/track.csv")
[ "$pts" -ge 4100 ] && [ "$pts" -le 4300 ] || fail "expected about 4207 sample rows, got $pts"
need_grep '^lane, 1, 0, -3, driving$' "$WORK/track.csv" "the third right-hand driving lane"

# A file that is not an OpenDRIVE network is a reported failure and produces
# no output file.
rm -f "$WORK/track.csv"
printf 'not xodr\n' > "$WORK/bad.xodr"
run_tool "$ODRPLOT" "$WORK/bad.xodr"
rc=$?
[ "$rc" -eq 0 ] && fail "odrplot reported success on a non-OpenDRIVE file"
[ -f "$WORK/track.csv" ] && fail "odrplot wrote track.csv for a non-OpenDRIVE file"
exit 0
