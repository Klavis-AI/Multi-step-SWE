#!/usr/bin/env bash
# Gate (proof of life). odrplot must sample an OpenDRIVE road's lane geometry into a CSV
# file: one "lane" header line per lane, followed by one x,y,z,h sample row
# per sampling step.
#
# R1's closing obligation (odrplot on STRING road ids) is deliberately NOT
# checked here: it is a new capability of this release, so it belongs to the
# new-tier string-road-ids group (cases/string-road-ids/a-road-id-mapping.sh),
# not to a gate group that must keep passing at the start tag.
source /tests/caselib.sh

cp "$FIXTURES/curves_elevation.xodr" "$WORK/curves_elevation.xodr"

run_tool "$ODRPLOT" curves_elevation.xodr out.csv 5.0
need_status 0 $? "odrplot"
need_file "$WORK/out.csv" "odrplot output"

lines=$(wc -l < "$WORK/out.csv")
[ "$lines" -eq 1624 ] || fail "expected 1624 lines in odrplot output, got $lines"

sed -n '1p' "$WORK/out.csv" > "$WORK/l1"
need_grep '^lane, 1, 0, 3, no-driving$' "$WORK/l1" "lane header line"

sed -n '2p' "$WORK/out.csv" > "$WORK/l2"
need_grep '^0\.000000, 14\.070000, 0\.000000, 0\.000000$' "$WORK/l2" "first sample row"
exit 0
