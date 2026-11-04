#!/usr/bin/env bash
# New. A speed action injected from the C ABI must take effect immediately
# (reported as ongoing) and the object must reach the injected target speed
# once its transition time has elapsed.
#
# NOTE: this file deliberately holds TWO case bodies' worth of assertions and
# is the only file in this group, because the group's declared case count (2)
# is fixed and the other file carries the override half. The `lateral` half
# below covers the *type-filtered* quarter of SE_InjectedActionOngoing, which
# the speed half cannot reach: an injected speed action is the only action
# running, so `any` and `LONG_SPEED` never disagree in that run.
source /tests/caselib.sh

cp "$FIXTURES/inject_cruise.xosc" "$WORK/inject_cruise.xosc"
cp "$FIXTURES/curves_elevation.xodr" "$WORK/curves_elevation.xodr"

run_driver d_inject inject_cruise.xosc 15 2
need_status 0 $? "d_inject"

need_kv ongoing_before 0 "no injected action before one is injected"
need_close speed_before 10.0 0.01 "the scenario's own initial cruise speed"
need_kv ongoing_at_inject 1 "the injected action reports ongoing immediately after injection"
need_kv ongoing_after 0 "the injected action reports finished once it has settled"
need_close speed_final 15.0 0.01 "the object reaches the injected target speed"
need_kv survived 1 "driver completed"

# The type-filtered half. SE_InjectedActionOngoing takes an ActionType index or
# -1 for "any type" (instruction R3 / esminiLib.hpp); R05.5 names the lane
# change and lane offset injectors as part of the same surface, and nothing
# else in the suite calls either of them.
run_driver d_inject inject_cruise.xosc lateral
need_status 0 $? "d_inject lateral"

need_kv any_before 0 "no action of any type before injection"
need_kv lane_change_before 0 "no lane change action before injection"
need_kv lane_offset_before 0 "no lane offset action before injection"

# After injecting a lane OFFSET action: that type is ongoing, "any" is
# ongoing, and the lane CHANGE type must still answer false.
need_kv lane_offset_at_offset_inject 1 "the injected lane offset action reports ongoing"
need_kv any_at_offset_inject 1 "the any-type query agrees an action is ongoing"
need_kv lane_change_at_offset_inject 0 "a query for a type that is not running must answer false"
need_kv speed_at_offset_inject 0 "a type that no injected action carries must answer false"

# After stacking a lane CHANGE action on top: both types are ongoing, so a
# build that reported only the any-type answer, or that answered true for
# every positive type, cannot produce all four of these lines at once.
need_kv any_at_change_inject 1 "an action of some type is ongoing"
need_kv lane_change_at_change_inject 1 "the injected lane change action reports ongoing"
need_kv lane_offset_at_change_inject 1 "the earlier lane offset action is still ongoing"
need_kv lane_offset_mid_neg 1 "the injected offset moved the object to the requested side of the lane center"
need_kv lane_id_final 1 "the injected lane change moved the object to the requested lane"
need_close lane_offset_final 0.0 0.05 "the lane offset action leaves the object back on the lane center once it finishes"

need_kv any_after 0 "both actions have finished"
need_kv lane_change_after 0 "the lane change action reports finished"
need_kv lane_offset_after 0 "the lane offset action reports finished"
need_kv survived 1 "driver completed"
exit 0
