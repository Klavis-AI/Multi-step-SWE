#!/usr/bin/env bash
# --csv_logger output is unchanged across this step: the six-line preamble, the
# column header and the formatting of every sample row are byte for byte what
# v2.59.0 produced. The double-precision migration touches the library API, not
# this file format.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_esmini --osc "$WORK/two_car_brake.xosc" --headless --fixed_timestep 0.05 \
           --csv_logger "$WORK/out.csv"
need_status 0 $? "esmini with --csv_logger"
need_file "$WORK/out.csv"

need_grep '^esmini GIT REV: ' "$WORK/out.csv" "preamble line 1"
need_grep '^Scenario File Name: ' "$WORK/out.csv" "preamble scenario name"
need_grep '^Number of Vehicles: 2$' "$WORK/out.csv" "preamble vehicle count"

# The header still spells the entity column "Entity_Name" -- not "Entity" and
# not "Entitity" -- and still carries NO space before a comma; the separator is
# a comma followed by a space. (Earlier steps in this package assert the spaced
# form "Index [-] , TimeStamp [s] , "; upstream dropped that space before this
# step's tags, so do not carry the older comment forward.) The spacing is also
# not uniform across the whole line -- two of the header's 64 commas run
# straight into the next field name, identically at both tags -- so what is
# pinned below is the prefix and the tail, not a rule about every separator.
need_grep '^Index \[-\], TimeStamp \[s\], #1 Entity_Name \[-\], #1 Entity_ID \[-\], ' \
    "$WORK/out.csv" "column header prefix"
need_grep '#2 collision_ids, $' "$WORK/out.csv" "column header tail"

# The first sample row still formats with six decimals and the Ego at 20 m/s.
need_grep '^0, 0\.000000, Ego, 0, 20\.000000, ' "$WORK/out.csv" "first Ego sample"

# Two entities, 10 s at 0.05 s.
rows=$(grep -c '^[0-9]' "$WORK/out.csv")
[ "$rows" -ge 190 ] && [ "$rows" -le 210 ] || fail "expected ~201 sample rows, got $rows"
