#!/usr/bin/env bash
# R09.3's own scenario: 10000 steps of 0.0001 s landing on exactly 1.0.
#
# This is a stronger float-failure case than 3600 x 0.001, and it is a different
# one rather than a restatement. 0.0001 is not representable in binary either,
# but 1.0 is exactly representable, so the whole of the error left at the end is
# accumulation error - there is no representation slack in the target to hide
# behind. A 32-bit accumulator drifts to ~5e-6 here (~40 ticks), which is four
# million times the 1e-12 asserted below. The driver is the same one
# accumulation.sh uses; nothing about the driver changes, only the arithmetic
# the case owns.
source /tests/caselib.sh

run_driver se_time "$FIXTURES/two_car_brake.xosc" 10000 0.0001
need_status 0 $? "se_time"
need_kv init 0 "SE_Init"
need_kv stepped 10000 "stepped count"
need_kv survived 1 "SE_Close"

# The clock starts at zero, so the total is the accumulation and nothing else.
need_close t0 0.0 1e-15 "clock at t0"

# 10000 * 0.0001 is 1.0 exactly. The tolerance is the requirement's: 1e-12.
t=$(sed -n 's/^t=//p' "$WORK/last.out")
[ -n "$t" ] || fail "driver printed no t= line"
awk -v t="$t" 'BEGIN { e = t - 1.0; if (e < 0) e = -e; exit (e <= 1e-12) ? 0 : 1 }' \
    || fail "time precision: |$t - 1.0| exceeds 1e-12, got $t"

# A float accumulator is the failure this pins, and the shape of it is a shortfall
# rather than an overshoot - the additions each round down. Asserting the
# direction as well keeps the case from being satisfied by a value that is merely
# close in the other direction, which is what a clock that over-advanced by some
# other route would produce.
awk -v t="$t" 'BEGIN { exit (t <= 1.0) ? 0 : 1 }' \
    || fail "accumulated clock overshot 1.0: $t"

# The same two reads as the other case: reading must not advance the clock, and
# the two must agree bit for bit.
t1=$(sed -n 's/^t=//p' "$WORK/last.out")
t2=$(sed -n 's/^t_again=//p' "$WORK/last.out")
[ "$t1" = "$t2" ] || fail "t=$t1 but t_again=$t2, clock is not stable"
