#!/usr/bin/env bash
# Angular ACCELERATION is a separate entry point from angular velocity and has
# a different signature in the data: entering the arc is a step change in
# curvature, so the heading acceleration spikes there and settles to ~0 in
# steady cornering, while on a straight road it never leaves 0.
#
# The size of the spike is deliberately not pinned - it is a one-step
# difference and therefore a function of the timestep, not of the road. What is
# graded is that the transient EXISTS on the arc and is ABSENT on the straight,
# which is exactly what separates a real implementation from one returning a
# constant.
source /tests/caselib.sh

run_driver se_motion "$FIXTURES/cruise_curve.xosc" 800 0.01 || fail "se_motion (curve) exited $?"
need_kv survived 1 "curve run completed"
need_kv rc_angacc 0 "SE_GetObjectAngularAcceleration succeeded"
curve_peak=$(sed -n 's/^h_acc_peak=//p' "$WORK/last.out" | tail -1)
[ -n "$curve_peak" ] || fail "curve run printed no h_acc_peak"

# Steady cornering: the rate is constant, so its derivative is back to zero by
# the end of the run even though it spiked on entry.
need_close h_acc 0.0 0.5 "heading acceleration has settled in steady cornering"
need_close p_acc 0.0 0.0001 "pitch acceleration stays zero"
need_close r_acc 0.0 0.0001 "roll acceleration stays zero"

run_driver se_motion "$FIXTURES/cruise_straight.xosc" 500 0.01 || fail "se_motion (straight) exited $?"
need_kv survived 1 "straight run completed"
need_kv rc_angacc 0 "SE_GetObjectAngularAcceleration succeeded on the straight"
straight_peak=$(sed -n 's/^h_acc_peak=//p' "$WORK/last.out" | tail -1)
[ -n "$straight_peak" ] || fail "straight run printed no h_acc_peak"

# A straight road never accelerates the heading at all.
awk -v p="$straight_peak" 'BEGIN { if (p < 0) p = -p; exit (p < 0.0001) ? 0 : 1 }' \
    || fail "straight road reported a heading acceleration peak of $straight_peak"

# The arc does, by orders of magnitude.
awk -v c="$curve_peak" 'BEGIN { if (c < 0) c = -c; exit (c > 1.0) ? 0 : 1 }' \
    || fail "arc entry produced no heading-acceleration transient (peak $curve_peak)"

# The bad-handle path is still reported rather than silently succeeding.
need_neg rc_angacc_bad "unknown object id reports failure"
need_neg rc_angvel_bad "unknown object id reports failure for angular velocity"
exit 0
