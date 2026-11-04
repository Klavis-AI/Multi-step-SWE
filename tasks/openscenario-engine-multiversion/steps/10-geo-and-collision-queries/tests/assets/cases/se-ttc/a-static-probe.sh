#!/usr/bin/env bash
# SE_GetTimeToCollision, new at this step's target. Probed at t=0 on a pair
# whose geometry makes the answer arithmetic rather than incidental: the Ego
# starts at s=20 doing 20 m/s, the Target at s=120 doing 10 m/s, so the
# reference points close a 100 m gap at 10 m/s and the bounding boxes close
# 95 m of it (100 m minus one 5 m car length).
#
# Every number below was read off the running engine, not derived from the
# standard: the coordinate-system and distance-type sweeps are what this build
# answers, including what it does with a request it cannot resolve.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_ttc "$WORK/two_car_brake.xosc" static
need_status 0 $? "se_ttc static"
need_kv init 0 "SE_Init"
need_kv survived 1 "SE_Close"

# A null output pointer is a documented failure, not a crash.
need_kv null_ttc -1 "null ttc pointer"

# Coordinate systems 1..4 (entity, lane, road, trajectory) all see the same
# longitudinal closing pair. 0 (CS_UNDEFINED) and 5 (CS_WORLD) are the two
# coordinate systems instruction requirement 1 names as unresolvable; neither is
# an error, and because THIS pair is closing the unresolved distance surfaces
# as an absurdly large positive time (instruction requirement 1). What the
# instruction promises there is a magnitude, not a constant -- the value is
# the road manager's stand-in distance divided by this fixture's closing
# speed, so re-cutting the fixture moves it -- so the bar asserted here is the
# documented floor of 1e8 s, i.e. nine or more digits before the decimal
# point, not the 1000000000.000000 this particular pairing happens to print.
# The resolvable answers are compared as numbers, not as printed strings: the
# arithmetic is what the instruction promises, the six-decimal formatting is
# not.
for cs in 1 2 3 4; do
    need_kv "cs${cs}_rc" 0 "coordinate system $cs is a success"
    need_close "cs${cs}_ttc" 10 1e-6 "TTC in coordinate system $cs"
done
for cs in 0 5; do
    need_grep "^cs$cs rc=0 ttc=[0-9]{9,}\.[0-9]+$" "$WORK/last.out" \
        "unresolvable coordinate system $cs is absurdly large, not an error"
done

# Distance types: undefined cannot be resolved and so is absurdly large on
# this closing pair, purely lateral never closes so it is the documented -1,
# and longitudinal / cartesian / euclidian all give 10 s.
need_grep '^dt0 rc=0 ttc=[0-9]{9,}\.[0-9]+$' "$WORK/last.out" \
    "REL_DIST_UNDEFINED is unresolvable, not an error"
# -1.0 is a sentinel and is matched exactly, on purpose: the contract names
# that value, so "close to -1" would not be the same promise.
need_grep '^dt1 rc=0 ttc=-1\.000000$' "$WORK/last.out" "REL_DIST_LATERAL never closes"
for dt in 2 3 4; do
    need_kv "dt${dt}_rc" 0 "distance type $dt is a success"
    need_close "dt${dt}_ttc" 10 1e-6 "distance type $dt"
done

# free_space measures between bounding boxes, so it must come out exactly one
# car length (5 m at 10 m/s = 0.5 s) earlier than the reference-point answer.
need_kv fs0_rc 0 "reference-point measurement is a success"
need_kv fs1_rc 0 "free-space measurement is a success"
need_close fs0_ttc 10 1e-6 "reference-point TTC"
need_close fs1_ttc 9.5 1e-6 "free-space TTC"

# TTC is symmetric in this configuration: asking from the target about the
# source gives the same closing time.
need_kv reversed_rc 0 "reversed argument order is a success"
need_close reversed_ttc 10 1e-6 "reversed argument order"

# Unknown object ids are a documented failure on either side.
need_kv bad_target -1 "unknown target id"
need_kv bad_source -1 "unknown source id"
need_kv both_bad -1 "both ids unknown"
