#!/usr/bin/env bash
# --csv_logger writes the six-line preamble and the column header the starting
# release already produced, byte for byte -- including the misspelling of
# "Entity" and the space before every comma. Neither changes at this step.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_esmini --osc "$WORK/two_car_brake.xosc" --headless --fixed_timestep 0.05 \
           --csv_logger "$WORK/out.csv"
need_status 0 $? "esmini with --csv_logger"
need_file "$WORK/out.csv"

# All six preamble lines, in the order instruction.md lists them.
need_grep '^esmini GIT REV: ' "$WORK/out.csv" "preamble line 1, GIT REV"
need_grep '^esmini GIT TAG: ' "$WORK/out.csv" "preamble line 2, GIT TAG"
need_grep '^esmini GIT BRANCH: ' "$WORK/out.csv" "preamble line 3, GIT BRANCH"
need_grep '^esmini BUILD VERSION: ' "$WORK/out.csv" "preamble line 4, BUILD VERSION"
need_grep '^Scenario File Name: ' "$WORK/out.csv" "preamble line 5, scenario name"
need_grep '^Number of Vehicles: 2$' "$WORK/out.csv" "preamble line 6, vehicle count"

order=$(head -6 "$WORK/out.csv" | sed -e 's/:.*/:/' | tr '\n' '|')
[ "$order" = 'esmini GIT REV:|esmini GIT TAG:|esmini GIT BRANCH:|esmini BUILD VERSION:|Scenario File Name:|Number of Vehicles:|' ] \
    || fail "csv_logger preamble order changed: $order"

hdr=$(grep -m1 '^Index \[-\]' "$WORK/out.csv") || fail "no column header row in out.csv"
case "$hdr" in
    'Index [-] , TimeStamp [s] , #1 Entitity_Name [-] , #1 Entitity_ID [-] , #1 Current_Speed [m/s] , '*) ;;
    *) fail "column header changed: $hdr" ;;
esac
grep -q '#1 Wheel_Angle \[deg\] , ' "$WORK/out.csv" || fail "Wheel_Angle column missing"
grep -q '#1 collision_ids , ' "$WORK/out.csv" || fail "collision_ids column missing"

# Two entities, 10 s at 0.05 s, plus the preamble and the header.
rows=$(grep -c '^[0-9]' "$WORK/out.csv")
[ "$rows" -ge 190 ] && [ "$rows" -le 210 ] || fail "expected ~200 sample rows, got $rows"
