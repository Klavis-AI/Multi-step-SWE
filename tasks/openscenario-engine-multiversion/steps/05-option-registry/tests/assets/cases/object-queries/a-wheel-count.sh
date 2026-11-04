#!/usr/bin/env bash
# New. A live scenario object reports a wheel count directly (a car has four),
# and an object id that does not exist must report the documented error value
# rather than an arbitrary number.
source /tests/caselib.sh

cp "$FIXTURES/brake_car.xosc" "$WORK/brake_car.xosc"
cp "$FIXTURES/curves_elevation.xodr" "$WORK/curves_elevation.xodr"

run_driver d_object_query brake_car.xosc
need_status 0 $? "d_object_query"

need_kv nobjs 1 "one object in the scenario"
need_kv wheels 4 "the car object has four wheels"
# R4 L86 promises only a SIGN ("a negative value if the object id does not
# exist"), so the magnitude is not pinned: -2 is as conforming as -1.
need_neg wheels_bad "a nonexistent object id reports a negative value"
need_kv survived 1 "driver completed"
exit 0
