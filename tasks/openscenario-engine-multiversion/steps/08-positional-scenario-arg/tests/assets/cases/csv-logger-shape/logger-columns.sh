#!/usr/bin/env bash
source /tests/caselib.sh

# --csv_logger writes a per-frame state table. Its layout is a six-line preamble
# identifying the build and the scenario, one header row naming every column,
# then one row per simulated frame. This whole file was measured byte-for-byte
# identical at both ends of this step, so the exact line count holds before and
# after the change this step asks for.
#
# esmini only reads the scenario, and the log is written relative to the working
# directory, which test.sh sets to $WORK - nothing is written into $FIXTURES.

run_esmini --osc "$FIXTURES/two_car_brake.xosc" --headless --fixed_timestep 0.05 \
    --csv_logger "$WORK/mylog.csv"
need_status 0 $? "esmini run with --csv_logger"
need_file "$WORK/mylog.csv"

lines=$(wc -l < "$WORK/mylog.csv")
[ "$lines" -eq 209 ] || fail "expected 209 lines in the csv log, got $lines"

# The six preamble lines, in order: build identity, then what was run.
sed -n '1p' "$WORK/mylog.csv" | grep -Eq '^esmini GIT REV: '       || fail "line 1 is not the GIT REV line"
sed -n '2p' "$WORK/mylog.csv" | grep -Eq '^esmini GIT TAG: '       || fail "line 2 is not the GIT TAG line"
sed -n '3p' "$WORK/mylog.csv" | grep -Eq '^esmini GIT BRANCH: '    || fail "line 3 is not the GIT BRANCH line"
sed -n '4p' "$WORK/mylog.csv" | grep -Eq '^esmini BUILD VERSION: ' || fail "line 4 is not the BUILD VERSION line"
sed -n '5p' "$WORK/mylog.csv" | grep -Eq '^Scenario File Name: .*two_car_brake' \
    || fail "line 5 does not name the scenario that was run"
sed -n '6p' "$WORK/mylog.csv" | grep -Eq '^Number of Vehicles: 2$' \
    || fail "line 6 does not report the two vehicles in the scenario"

# Line 7 is the header. Columns are asserted by name, not by position, except
# for the two leading ones which the format fixes.
sed -n '7p' "$WORK/mylog.csv" > "$WORK/header.csv"
idx=$(csv_column 'Index [-]' "$WORK/header.csv")
[ "$idx" = "1" ] || fail "Index column expected first, got position '${idx:-absent}'"
ts=$(csv_column 'TimeStamp [s]' "$WORK/header.csv")
[ "$ts" = "2" ] || fail "TimeStamp column expected second, got position '${ts:-absent}'"

for col in '#1 Entity_Name [-]' '#1 Entity_ID [-]' '#1 Current_Speed [m/s]' \
           '#1 World_Position_X [m]' '#1 World_Position_Y [m]' '#1 World_Heading_Angle [rad]' \
           '#1 lane_id'; do
    [ -n "$(csv_column "$col" "$WORK/header.csv")" ] || fail "header is missing the '$col' column"
done

# The scenario has two vehicles, so the per-entity columns are repeated for the
# second one after the first one's block.
v1=$(csv_column '#1 Entity_Name [-]' "$WORK/header.csv")
v2=$(csv_column '#2 Entity_Name [-]' "$WORK/header.csv")
[ -n "$v2" ] || fail "header has no columns for the second vehicle"
[ "$v2" -gt "$v1" ] || fail "second vehicle's columns precede the first vehicle's"
[ -n "$(csv_column '#2 Current_Speed [m/s]' "$WORK/header.csv")" ] \
    || fail "second vehicle has no speed column"

# 209 lines minus six preamble lines minus one header is 202 frames, each
# beginning with its frame index and timestamp.
rows=$(grep -c '^[0-9]\+, ' "$WORK/mylog.csv")
[ "$rows" -eq 202 ] || fail "expected 202 frame rows, got $rows"
sed -n '8p' "$WORK/mylog.csv" | grep -Eq '^0, ' || fail "first frame is not index 0"
tail -1 "$WORK/mylog.csv" | grep -Eq '^201, ' || fail "last frame is not index 201"

# Frame indices increase by one, with no gaps or repeats.
seq 0 201 > "$WORK/expected_idx"
grep -o '^[0-9]\+' "$WORK/mylog.csv" > "$WORK/actual_idx"
diff -q "$WORK/expected_idx" "$WORK/actual_idx" >/dev/null \
    || fail "frame indices are not the consecutive run 0..201"
exit 0
