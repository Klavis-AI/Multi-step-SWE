#!/usr/bin/env bash
# The other half of the time-to-collision contract: the answer has to track the
# simulation rather than be computed once at load time, and a pair that is not
# closing at all has to be reported as "no collision" rather than as a very
# large number.
#
# two_car_brake.xosc puts the Ego 100 m behind the Target at 20 m/s against
# 10 m/s, and brakes the Ego to 5 m/s from t = 3 s. So the gap closes at 10 m/s
# to begin with, keeps closing more slowly while the Ego decelerates, and stops
# closing altogether once the Ego is slower than the Target. Every number below
# was read off the running engine at the target release.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" \
   "$FIXTURES/tri_lane_cars.xosc" "$FIXTURES/tri_lane_600m.xodr" "$WORK/"

run_driver se_ttc "$WORK/two_car_brake.xosc" dynamic
need_status 0 $? "se_ttc dynamic"
need_kv init 0 "SE_Init"
need_kv survived 1 "SE_Close"

# The engine was sampled at four simulation times; the times themselves are
# asserted by the human-readable lines, the numbers by value rather than by
# printed format.
for k in t0 t2 t5 t9; do
    need_kv "${k}_rc" 0 "$k is a success"
    need_kv "${k}_free_rc" 0 "$k free space is a success"
done
need_grep '^t0\.00 rc=0 ' "$WORK/last.out" "sampled at t=0"
need_grep '^t2\.00 rc=0 ' "$WORK/last.out" "sampled at t=2"
need_grep '^t5\.00 rc=0 ' "$WORK/last.out" "sampled at t=5"
need_grep '^t9\.00 rc=0 ' "$WORK/last.out" "sampled at t=9"

# t = 0: 100 m at 10 m/s closing, one 5 m car length less between the boxes.
need_close t0_ttc 10 1e-6 "TTC at t=0"
need_close t0_free_ttc 9.5 1e-6 "free-space TTC at t=0"

# t = 2 s: still both at constant speed, so exactly two seconds have been eaten.
need_close t2_ttc 8 1e-6 "TTC at t=2"
need_close t2_free_ttc 7.5 1e-6 "free-space TTC at t=2"

# t = 5 s: the Ego has been braking for two seconds, so the closing speed has
# collapsed and the predicted time has gone UP rather than down. Measured at
# 13.457831 / 12.253012; compared with a tolerance wide enough that the last
# bits of a floating-point division cannot decide the case, and narrow enough
# that only a build doing this arithmetic can land in it.
need_close t5_ttc 13.4578 0.01 "TTC grows once the follower brakes"
need_close t5_free_ttc 12.2530 0.01 "free-space TTC at t=5"

# t = 9 s: the Ego is down to 5 m/s against the Target's 10 m/s. The pair is
# separating, and a separating pair is the documented -1. That one is a
# sentinel, so it is matched exactly rather than approximately.
need_grep '^t9\.00 rc=0 ttc=-1\.000000$' "$WORK/last.out" "separating pair reports no collision"
need_grep '^t9\.00 free rc=0 ttc=-1\.000000$' "$WORK/last.out" "separating pair, free space"

# Negative control on a different network: four cars standing still on four
# different lanes. Nothing closes, in any coordinate system, with any distance
# type, either way round. This is the ordering clause of instruction
# requirement 1: the closing speed is tested FIRST, so cs0/cs5 and dt0 -- the
# requests the engine cannot resolve, which on the closing pair of case a come
# back absurdly large -- must here come back -1.0 like everything else. A
# build that let the unresolved distance win over the not-closing check would
# answer with a huge number instead, so the sweep is followed by a blanket
# refusal of any absurdly large answer anywhere in the output.
run_driver se_ttc "$WORK/tri_lane_cars.xosc" static
need_status 0 $? "se_ttc static on a parked scenario"
need_kv init 0 "SE_Init on tri_lane_cars"
need_kv survived 1 "SE_Close on tri_lane_cars"
for cs in 0 1 2 3 4 5; do
    need_grep "^cs$cs rc=0 ttc=-1\.000000$" "$WORK/last.out" "parked pair, coordinate system $cs"
done
for dt in 0 1 2 3 4; do
    need_grep "^dt$dt rc=0 ttc=-1\.000000$" "$WORK/last.out" "parked pair, distance type $dt"
done
need_grep '^fs0 rc=0 ttc=-1\.000000$' "$WORK/last.out" "parked pair, reference points"
need_grep '^fs1 rc=0 ttc=-1\.000000$' "$WORK/last.out" "parked pair, free space"
need_grep '^reversed rc=0 ttc=-1\.000000$' "$WORK/last.out" "parked pair, reversed"
need_not_grep 'ttc=[0-9]{9,}\.' "$WORK/last.out" \
    "an unresolvable request never outranks the not-closing answer"
