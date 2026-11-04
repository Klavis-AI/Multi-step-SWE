#!/usr/bin/env bash
# Angular velocity is a physical quantity, not a constant the library can fake.
# Both fixtures drive the SAME arc (curve_r100.xodr), one at 20 m/s and one at
# 12 m/s, so the heading rate v/r must come out in the ratio 12/20 = 0.6.
#
# The ratio is asserted rather than either absolute rate because it cancels the
# arc's effective radius: a stub returning any fixed value, or a rate that does
# not respond to speed at all, fails this even though it could satisfy a single
# absolute check tuned to one fixture.
source /tests/caselib.sh

run_driver se_motion "$FIXTURES/cruise_curve.xosc" 800 0.01 || fail "se_motion (fast) exited $?"
need_kv survived 1 "fast run completed"
need_kv rc_angvel 0 "SE_GetObjectAngularVelocity succeeded on the fast run"
fast=$(sed -n 's/^h_rate=//p' "$WORK/last.out" | tail -1)
[ -n "$fast" ] || fail "fast run printed no h_rate"
need_close vel_norm 20.0 0.01 "fast run holds 20 m/s"

run_driver se_motion "$FIXTURES/cruise_curve_slow.xosc" 800 0.01 || fail "se_motion (slow) exited $?"
need_kv survived 1 "slow run completed"
need_kv rc_angvel 0 "SE_GetObjectAngularVelocity succeeded on the slow run"
slow=$(sed -n 's/^h_rate=//p' "$WORK/last.out" | tail -1)
[ -n "$slow" ] || fail "slow run printed no h_rate"
need_close vel_norm 12.0 0.01 "slow run holds 12 m/s"

# Both turn the same way on the same arc, and the slower one turns more slowly.
awk -v f="$fast" 'BEGIN { exit (f > 0.01) ? 0 : 1 }' || fail "fast heading rate $fast is not a real turn"
awk -v s="$slow" 'BEGIN { exit (s > 0.01) ? 0 : 1 }' || fail "slow heading rate $slow is not a real turn"
awk -v f="$fast" -v s="$slow" 'BEGIN { exit (s < f) ? 0 : 1 }' \
    || fail "slow heading rate $slow is not below the fast one $fast"

# v/r scales linearly with v on a fixed radius.
awk -v f="$fast" -v s="$slow" 'BEGIN {
    r = s / f; d = r - 0.6; if (d < 0) d = -d
    exit (d <= 0.02) ? 0 : 1
}' || fail "heading-rate ratio slow/fast = $slow/$fast is not the speed ratio 0.6"
exit 0
