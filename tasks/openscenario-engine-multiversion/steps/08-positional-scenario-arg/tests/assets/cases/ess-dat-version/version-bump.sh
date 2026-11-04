#!/usr/bin/env bash
source /tests/caselib.sh

# The packet format is v4.3 at the target release. The first line of a dat2csv
# plain CSV output must list version 4, and --file_refs must list 4.3.

run_esmini --osc "$FIXTURES/two_car_brake.xosc" \
    --headless --fixed_timestep 0.05 --record sim --disable_stdout
need_status 0 $? "run_esmini"
need_file "$WORK/sim.dat"

run_tool "$DAT2CSV" --file "$WORK/sim.dat"
need_status 0 $? "dat2csv plain"
need_file "$WORK/sim.csv"
need_grep '^time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot' "$WORK/sim.csv" "plain header"

run_tool "$DAT2CSV" --file_refs --print_csv --file "$WORK/sim.dat"
need_status 0 $? "dat2csv with file_refs"
need_grep '^Version: 4\.3,' "$WORK/last.out" "file_refs version 4.3"
