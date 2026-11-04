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
# the .dat it read. The .dat is deliberately read from another directory so the
# two readings differ: a decoder that wrote beside its input would leave
# rec.csv next to the .dat and nothing in the run directory.
mkdir -p "$WORK/elsewhere"
( cd "$WORK/elsewhere" && run_tool "$DAT2CSV" "$WORK/rec.dat" )
need_status 0 $? "dat2csv"
need_file "$WORK/elsewhere/rec.csv"
[ -f "$WORK/rec.csv" ] && fail "dat2csv wrote its output beside the .dat instead of into the current directory"

need_grep '^Version: 2, OpenDRIVE: straight_500m.xodr, 3DModel: ' "$WORK/elsewhere/rec.csv" "dat2csv file header"
need_grep '^time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot$' "$WORK/elsewhere/rec.csv" "dat2csv column header"
need_grep '^0.000, 0, Ego, ' "$WORK/elsewhere/rec.csv" "first Ego sample"
need_grep ', 1, Target, ' "$WORK/elsewhere/rec.csv" "Target samples present"

# Invoked with no argument dat2csv prints its usage string and refuses; the
# frozen requirement names that print, so it is asserted rather than assumed.
run_tool "$DAT2CSV"
[ "$?" -eq 0 ] && fail "dat2csv reported success with no input file"
need_grep '^Usage: .*dat2csv <filename>$' "$WORK/last.out" "dat2csv usage string"

# The Ego brakes from 20 m/s to 5 m/s; the recording has to show it.
last_ego=$(awk -F', ' '$3 == "Ego" { v = $10 } END { print v + 0 }' "$WORK/elsewhere/rec.csv")
awk -v v="$last_ego" 'BEGIN { exit !(v > 4.5 && v < 5.5) }' \
    || fail "Ego final speed in the recording was $last_ego, expected about 5"
