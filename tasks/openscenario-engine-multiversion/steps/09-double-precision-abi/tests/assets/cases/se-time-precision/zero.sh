#!/usr/bin/env bash
# SE_GetSimulationTime returns 0.0 before any steps are taken, and the value
# must be reported as double: the static assertion guarantees it at compile
# time, and the runtime check confirms SE_Init does not fabricate a time.
source /tests/caselib.sh

run_driver se_time "$FIXTURES/two_car_brake.xosc" 0 0.0
need_status 0 $? "se_time"
need_kv init 0 "SE_Init"
need_kv stepped 0 "zero steps taken"
need_kv survived 1 "SE_Close"

t0=$(sed -n 's/^t0=//p' "$WORK/last.out")
[ -n "$t0" ] || fail "driver printed no t0= line"
need_close t0 0.0 1e-15 "initial time"

t=$(sed -n 's/^t=//p' "$WORK/last.out")
[ -n "$t" ] || fail "driver printed no t= line"
[ "$t" = "$t0" ] || fail "time after 0 steps is $t, expected $t0"
