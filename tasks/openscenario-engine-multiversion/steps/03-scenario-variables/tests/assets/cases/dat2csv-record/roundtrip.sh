#!/usr/bin/env bash
# --record writes a .dat recording that dat2csv decodes. Format version 2 and
# the column header are both unchanged at this step.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_esmini --osc "$WORK/two_car_brake.xosc" --headless --fixed_timestep 0.05 \
           --record "$WORK/rec.dat"
need_status 0 $? "esmini with --record"
need_file "$WORK/rec.dat"

# dat2csv writes <input basename>.csv into the current directory, not next to
# the .dat, so the case runs it from its own scratch directory.
run_tool "$DAT2CSV" "$WORK/rec.dat"
need_status 0 $? "dat2csv"
need_file "$WORK/rec.csv"

need_grep '^Version: 2, OpenDRIVE: straight_500m.xodr, 3DModel: ' "$WORK/rec.csv" "dat2csv file header"
need_grep '^time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot$' "$WORK/rec.csv" "dat2csv column header"
need_grep '^0.000, 0, Ego, ' "$WORK/rec.csv" "first Ego sample"
need_grep ', 1, Target, ' "$WORK/rec.csv" "Target samples present"

# The Ego brakes from 20 m/s to 5 m/s; the recording has to show it.
last_ego=$(awk -F', ' '$3 == "Ego" { v = $10 } END { print v + 0 }' "$WORK/rec.csv")
awk -v v="$last_ego" 'BEGIN { exit !(v > 4.5 && v < 5.5) }' \
    || fail "Ego final speed in the recording was $last_ego, expected about 5"
