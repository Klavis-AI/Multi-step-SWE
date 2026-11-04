#!/usr/bin/env bash
# SE_GetDistanceToObject and SE_GetObjectState as they already behave at the
# starting release. The new cached query (SE_SimpleGetDistanceToObject) is
# asserted separately in se-distance-cached; this case exists so that a tree
# which adds the cached query by rewriting the legacy one is caught.
source /tests/caselib.sh

run_driver se_dist_legacy "$FIXTURES/two_car_brake.xosc" 300 0.01 || fail "se_dist_legacy exited $?"
need_kv survived 1 "driver ran to completion"
need_kv init 0 "SE_Init accepted the scenario"
need_kv n_objects 2 "two vehicles in the scenario"

# At t=0 the scenario teleports Ego to s=20 and Target to s=120 in the same
# lane, so they are exactly 100 m apart with no lateral offset.
need_kv rc_t0 0 "SE_GetDistanceToObject succeeded at t=0"
need_close ds_t0 100.0 0.001 "longitudinal separation at t=0"
need_close dt_t0 0.0 0.001 "lateral separation at t=0"

# Ego holds 20 m/s, Target brakes to 10 m/s, so over 3 s the gap closes by
# 30 m. Asserting the two speeds and the two s values as well as the gap
# means an engine that reported a plausible distance from the wrong pair of
# positions still fails.
need_close sim_time 3.0 0.001 "simulation time after 300 steps of 0.01"
need_kv rc_end 0 "SE_GetDistanceToObject succeeded at the end"
need_close ds_end 70.0 0.001 "longitudinal separation after 3 s"
need_close dt_end 0.0 0.001 "lateral separation after 3 s"
need_close obj0_speed 20.0 0.001 "Ego speed"
need_close obj1_speed 10.0 0.001 "Target speed"
need_close obj0_s 80.0 0.001 "Ego s"
need_close obj1_s 150.0 0.001 "Target s"

# The gap is the difference of the two s values, not an independent number.
ds=$(sed -n 's/^ds_end=//p' "$WORK/last.out" | tail -1)
s0=$(sed -n 's/^obj0_s=//p' "$WORK/last.out" | tail -1)
s1=$(sed -n 's/^obj1_s=//p' "$WORK/last.out" | tail -1)
awk -v a="$ds" -v b="$s1" -v c="$s0" 'BEGIN{exit !((b-c-a)<0.01 && (b-c-a)>-0.01)}' \
    || fail "ds_end ($ds) does not equal obj1_s - obj0_s ($s1 - $s0)"

# An object id that is not in the scenario must be refused, not answered.
need_neg rc_bad "SE_GetDistanceToObject rejects an unknown object id"
