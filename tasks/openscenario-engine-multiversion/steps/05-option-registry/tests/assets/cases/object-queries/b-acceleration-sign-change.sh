#!/usr/bin/env bash
# New. brake_car.xosc puts the object at its 20 m/s cruise speed from t=0
# (a step-shaped speed action) and brakes it hard from t=2 s, so a single run
# samples acceleration with two different, predictable SIGNS -- a stub
# returning one constant cannot pass both windows. Only the signs are graded:
# an engine that applies the step instantly reports ~0 during the cruise
# window and one that ramps under the vehicle's performance limits reports a
# small positive value, and R4 promises the sign convention, not a magnitude.
# The brake rate is the one number the fixture does pin (20 m/s to 0 over
# 2.0 s linear = -10 m/s^2) and is held to a loose tolerance.
source /tests/caselib.sh

cp "$FIXTURES/brake_car.xosc" "$WORK/brake_car.xosc"
cp "$FIXTURES/curves_elevation.xodr" "$WORK/curves_elevation.xodr"

run_driver d_object_query brake_car.xosc
need_status 0 $? "d_object_query"

need_kv advance_ok 1 "the driver's step loop reaches its intended simulated time"
need_kv cruise_known 1 "an acceleration sample was captured before braking starts"
need_kv brake_known 1 "an acceleration sample was captured while braking"
need_close speed_cruise 20.0 0.5 "speed at or approaching the 20 m/s cruise target"
need_kv accel_cruise_nonneg 1 "not decelerating during the cruise window (zero or positive)"
need_kv accel_brake_negative 1 "hard braking: negative"
need_close accel_brake -10.0 0.5 "brake rate pinned by the fixture: 20 m/s over 2.0 s linear"
need_kv survived 1 "driver completed"
exit 0
