#!/usr/bin/env bash
# Regression floor, second half. The collision bookkeeping answers from the
# current geometry, not from how long the scenario has been running and not
# from which road network it is running on. Measured identical at both ends of
# this step.
#
# First a different scenario on a different network (three lanes, four parked
# entities); then the same scenario as the companion case but with zero
# free-running steps before the overlap is forced, which must give the same
# before/after pair.
source /tests/caselib.sh

cp "$FIXTURES/tri_lane_cars.xosc" "$FIXTURES/tri_lane_600m.xodr" \
   "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_collision_legacy "$WORK/tri_lane_cars.xosc" 20
need_status 0 $? "se_collision_legacy on tri_lane_cars"
need_kv init 0 "SE_Init on the three-lane network"
need_kv ncoll_obj0_before 0 "entities in separate lanes do not collide"
need_kv ncoll_obj0_after 1 "one collision once an entity is moved on top"
need_kv coll0_partner 1 "the reported partner on the three-lane network"
need_kv badobj_ncoll -1 "out-of-range collision count"
need_kv badobj_coll -1 "out-of-range collision partner"
need_kv survived 1 "SE_Close on the three-lane network"

# Zero free-running steps: the overlap is forced at t=0.
run_driver se_collision_legacy "$WORK/two_car_brake.xosc" 0
need_status 0 $? "se_collision_legacy with no free-running steps"
need_kv init 0 "SE_Init with no free-running steps"
need_kv ncoll_obj0_before 0 "no collision at the initial placement"
need_kv ncoll_obj0_after 1 "one collision immediately after the overlap is forced"
need_kv coll0_partner 1 "the partner at t=0"
need_kv survived 1 "SE_Close with no free-running steps"
