#!/usr/bin/env bash
# The recording format major version changes from 4 to 5. esmini's own
# --record output is decoded by dat2csv --file_refs, whose first line reports
# the version it read. A v2.59.0 build writes 4 here.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_esmini --osc "$WORK/two_car_brake.xosc" --headless --fixed_timestep 0.05 \
           --record "$WORK/rec.dat"
need_status 0 $? "esmini with --record"
need_file "$WORK/rec.dat"

run_tool "$DAT2CSV" --file_refs "$WORK/rec.dat"
need_status 0 $? "dat2csv"

# dat2csv writes <basename>.csv beside the .dat; the file_refs preamble is its
# first line and carries the format version.
need_file "$WORK/rec.csv"
need_grep '^Version: 5\.1, ' "$WORK/rec.csv" "recording format version"
need_grep 'OpenDRIVE: .*straight_500m\.xodr' "$WORK/rec.csv" "ODR filename preserved"

# The decoded recording is still a two-entity 10 s run at 0.05 s.
need_grep '^time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot$' "$WORK/rec.csv" "column header"
rows=$(grep -c '^[0-9]' "$WORK/rec.csv")
[ "$rows" -ge 400 ] || fail "expected ~402 sample rows for 2 entities, got $rows"
