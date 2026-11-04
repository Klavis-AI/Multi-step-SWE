#!/usr/bin/env bash
# The limit is a parameter, not a constant: a four-times-larger limit has to
# shed four times the speed over the same wall time.
source /tests/caselib.sh

run_driver se_simple_vehicle 8.0 20 0.05 || fail "se_simple_vehicle exited $?"
need_close speed_final 22.0 0.05 "speed after 1 s at 8 m/s^2"
need_close worst_decel 8.0 0.05 "deceleration tracks the limit upwards"

run_driver se_simple_vehicle 0.5 20 0.05 || fail "se_simple_vehicle exited $?"
need_close speed_final 29.5 0.05 "speed after 1 s at 0.5 m/s^2"
need_close worst_decel 0.5 0.05 "deceleration tracks the limit downwards"
