#!/usr/bin/env bash
# SE_GetSimulationTime and SE_GetSimTimeStep return double, not float. The
# static assertion on the declared type fails at v2.59.0, and the accumulated
# value after 3600 steps at 0.001s shows precision no 32-bit float can reach.
source /tests/caselib.sh

run_driver se_time "$FIXTURES/two_car_brake.xosc" 3600 0.001
need_status 0 $? "se_time"
need_kv init 0 "SE_Init"
need_kv stepped 3600 "stepped count"
need_kv survived 1 "SE_Close"

# After 3600 * 0.001s the time must be within 1e-12 of 3.6. A float
# accumulator drifts to ~3.5999999 (error ~4e-7); a double reaches
# ~3.5999999999997145 (error ~3e-13).
t=$(sed -n 's/^t=//p' "$WORK/last.out")
[ -n "$t" ] || fail "driver printed no t= line"
awk -v t="$t" 'BEGIN { e = t - 3.6; if (e < 0) e = -e; exit (e <= 1e-12) ? 0 : 1 }' \
    || fail "time precision: |$t - 3.6| exceeds 1e-12, got $t"

# Repeated reads must be stable.
t1=$(sed -n 's/^t=//p' "$WORK/last.out")
t2=$(sed -n 's/^t_again=//p' "$WORK/last.out")
[ "$t1" = "$t2" ] || fail "t=$t1 but t_again=$t2, clock is not stable"
