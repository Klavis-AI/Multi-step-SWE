#!/usr/bin/env bash
# SE_ReportObject* entry points take double coordinates and no timestamp. The
# static assertion on the declared signature fails at v2.59.0, and the runtime
# check confirms that a value requiring >24 mantissa bits survives the round
# trip through SE_ReportObjectPos and SE_GetObjectState without truncation.
source /tests/caselib.sh

X=123.456789012345
Y=-1.53456789012345
SPD=17.1234567890123
ROT=1.5
ANG=0.25

run_driver se_report "$FIXTURES/two_car_brake.xosc" "$X" "$Y" "$SPD" "$ROT" "$ANG"
need_status 0 $? "se_report"
need_kv init 0 "SE_Init"
need_kv survived 1 "SE_Close"

# Before loading a scenario, all Report* functions must return an error rather
# than crash.
need_kv pos_before -1 "SE_ReportObjectPos before SE_Init"
need_kv speed_before -1 "SE_ReportObjectSpeed before SE_Init"
need_kv wheel_before -1 "SE_ReportObjectWheelStatus before SE_Init"

# After loading, entity 0 exists but 4242 does not.
need_kv pos_rc 0 "SE_ReportObjectPos on entity 0"
need_kv pos_rc_bad -1 "SE_ReportObjectPos on non-existent entity"
need_kv speed_rc 0 "SE_ReportObjectSpeed on entity 0"
need_kv speed_rc_bad -1 "SE_ReportObjectSpeed on non-existent entity"
need_kv wheel_rc 0 "SE_ReportObjectWheelStatus on entity 0"
need_kv wheel_rc_bad -1 "SE_ReportObjectWheelStatus on non-existent entity"

# The reported X must match the input to the tolerance the step documents. A
# float-narrowing implementation returns 123.456787109375 - an error of 1.9e-6,
# six orders of magnitude outside the 1e-12 asserted here.
need_kv get_rc 0 "SE_GetObjectState"
x_out=$(sed -n 's/^x=//p' "$WORK/last.out")
[ -n "$x_out" ] || fail "driver printed no x= line"
awk -v x="$x_out" -v e="$X" 'BEGIN { d = x - e; if (d < 0) d = -d; exit (d <= 1e-12) ? 0 : 1 }' \
    || fail "X precision: |$x_out - $X| exceeds 1e-12"

y_out=$(sed -n 's/^y=//p' "$WORK/last.out")
[ -n "$y_out" ] || fail "driver printed no y= line"
awk -v y="$y_out" -v e="$Y" 'BEGIN { d = y - e; if (d < 0) d = -d; exit (d <= 1e-12) ? 0 : 1 }' \
    || fail "Y precision: |$y_out - $Y| exceeds 1e-12"

# SE_ReportObjectRoadPos also takes double s, and the value survives.
s1_out=$(sed -n 's/^s1=//p' "$WORK/last.out")
[ -n "$s1_out" ] || fail "driver printed no s1= line"
awk -v s="$s1_out" 'BEGIN { e = 234.56789012345; d = s - e; if (d < 0) d = -d; exit (d <= 1e-12) ? 0 : 1 }' \
    || fail "road s precision: |$s1_out - 234.56789012345| exceeds 1e-12"
