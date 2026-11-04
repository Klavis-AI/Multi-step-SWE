#!/usr/bin/env bash
source /tests/caselib.sh

# The output CSV is written alongside the input .dat file by default,
# not in the current directory.

run_esmini --osc "$FIXTURES/two_car_brake.xosc" \
    --headless --fixed_timestep 0.05 --record sim --disable_stdout
need_status 0 $? "run_esmini"
need_file "$WORK/sim.dat"

mkdir -p "$WORK/sub"
mv "$WORK/sim.dat" "$WORK/sub/"

run_tool "$DAT2CSV" "$WORK/sub/sim.dat"
need_status 0 $? "dat2csv"
need_file "$WORK/sub/sim.csv"
[ ! -e "$WORK/sim.csv" ] || fail "csv should not exist in cwd"
