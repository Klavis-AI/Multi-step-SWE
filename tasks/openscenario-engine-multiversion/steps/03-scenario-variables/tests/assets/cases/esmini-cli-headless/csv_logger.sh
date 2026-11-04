#!/usr/bin/env bash
# --csv_logger writes the preamble and the column header earlier releases
# already produced, byte for byte -- including the misspelling of "Entity" and
# the space before every comma. Neither changes at this step.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_esmini --osc "$WORK/two_car_brake.xosc" --headless --fixed_timestep 0.05 \
           --csv_logger "$WORK/out.csv"
need_status 0 $? "esmini with --csv_logger"
need_file "$WORK/out.csv"

need_grep '^esmini GIT REV: ' "$WORK/out.csv" "preamble line 1"
need_grep '^Scenario File Name: ' "$WORK/out.csv" "preamble scenario name"
need_grep '^Number of Vehicles: 2$' "$WORK/out.csv" "preamble vehicle count"

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
