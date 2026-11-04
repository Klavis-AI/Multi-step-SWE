#!/usr/bin/env bash
# Motion-state queries on a straight road, where every angular quantity is
# exactly zero and the odometer has to equal the distance travelled.
source /tests/caselib.sh

run_driver se_motion "$FIXTURES/cruise_straight.xosc" 500 0.01 || fail "se_motion exited $?"
need_kv survived 1 "driver ran to completion"

need_close odo 100.0 0.01 "odometer = 20 m/s * 5 s"
need_kv odo_monotonic 1 "odometer never decreases"
need_close odo_start 0.0 0.001 "odometer is 0 before the first step"
need_close sim_time 5.0 0.001 "simulation time"

# Velocity: 20 m/s along x, nothing sideways.
need_kv rc_vel 0 "SE_GetObjectVelocityGlobalXYZ succeeded"
need_close vel_x 20.0 0.001 "velocity x"
need_close vel_y 0.0 0.001 "velocity y"
need_close vel_z 0.0 0.001 "velocity z"
need_close vel_norm 20.0 0.001 "velocity magnitude"

# Angular velocity and acceleration are all zero on a straight road.
need_kv rc_angvel 0 "SE_GetObjectAngularVelocity succeeded"
need_close h_rate 0.0 0.0001 "heading rate"
need_close p_rate 0.0 0.0001 "pitch rate"
need_close r_rate 0.0 0.0001 "roll rate"
need_close h_rate_peak 0.0 0.0001 "worst heading rate over the run"

need_kv rc_angacc 0 "SE_GetObjectAngularAcceleration succeeded"
need_close h_acc 0.0 0.0001 "heading acceleration"
need_close p_acc 0.0 0.0001 "pitch acceleration"
need_close r_acc 0.0 0.0001 "roll acceleration"
need_close h_acc_peak 0.0 0.0001 "worst heading acceleration over the run"

# The rejection paths instruction.md's "How failures are reported" section says
# the checker exercises. The velocity query has a return code to spend and must
# spend it; the odometer has none, so its answer for an id no scenario defines
# has to be a NaN rather than a plausible distance. Both are asserted here
# because this is the case that already runs the driver that measures them.
need_neg rc_vel_bad "an object id no scenario defines is refused by the velocity query"
need_kv odo_bad_is_nan 1 "the odometer answers NaN for an object id no scenario defines"
