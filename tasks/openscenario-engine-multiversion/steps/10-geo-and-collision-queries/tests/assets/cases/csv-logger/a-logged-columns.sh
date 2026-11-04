#!/usr/bin/env bash
# Gate (proof of life). --csv_logger writes a per-timestep record of every entity, and
# neither its six-line preamble, nor its column header, nor the formatting of a
# sample row changes across this step. New queries added to the embedding ABI
# do not add, remove or rename a logged column.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_esmini --osc "$WORK/two_car_brake.xosc" --headless --fixed_timestep 0.05 \
           --csv_logger "$WORK/out.csv"
need_status 0 $? "esmini with --csv_logger"
need_file "$WORK/out.csv"

need_grep '^esmini GIT REV: ' "$WORK/out.csv" "preamble build identification"
need_grep '^Scenario File Name: ' "$WORK/out.csv" "preamble scenario name"
need_grep '^Number of Vehicles: 2$' "$WORK/out.csv" "preamble vehicle count"

# The header is still per-entity, still prefixed "#<n> ", and still ends with
# the collision column that the legacy collision bookkeeping feeds.
need_grep '^Index \[-\], TimeStamp \[s\], #1 Entity_Name \[-\], #1 Entity_ID \[-\], ' \
    "$WORK/out.csv" "column header prefix"
need_grep '#1 lane_id, ' "$WORK/out.csv" "per-entity lane column"
need_grep '#2 collision_ids, $' "$WORK/out.csv" "column header tail"

# First sample: Ego at its declared 20 m/s, six decimals, entity id 0.
need_grep '^0, 0\.000000, Ego, 0, 20\.000000, ' "$WORK/out.csv" "first Ego sample"
# The scenario brakes the Ego to 5 m/s and stops itself; the last row shows it.
need_grep '^[0-9]+, 10\.050000, Ego, 0, 5\.000000, ' "$WORK/out.csv" "last Ego sample"

rows=$(grep -c '^[0-9]' "$WORK/out.csv")
[ "$rows" -ge 195 ] && [ "$rows" -le 210 ] || fail "expected about 202 sample rows, got $rows"
