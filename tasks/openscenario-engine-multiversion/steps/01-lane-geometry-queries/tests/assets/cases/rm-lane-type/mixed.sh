#!/usr/bin/env bash
# Three different lane types on one road: DRIVING (2), STOP (4), BORDER (64).
# e6mini.xodr road 0 carries all three.
source /tests/caselib.sh

declare -A T=([-1]=64 [-2]=2 [-3]=2 [-4]=2 [-5]=4 [-6]=64 [-7]=64)

for lane in -1 -2 -3 -4 -5 -6 -7; do
    run_driver rm_lane "$FIXTURES/e6mini.xodr" 0 "$lane" 100 || fail "rm_lane lane $lane exited $?"
    need_kv type_by_road "${T[$lane]}" "RM_GetLaneTypeByRoadId(0,$lane,100)"
    need_kv type_handle "${T[$lane]}" "RM_GetLaneType(handle,$lane)"
done

# As with the widths: the handle form's lane comes from its argument, not from
# the lane the handle sits on. Asked from a single position parked on lane -2,
# it must still report each other lane's own type.
for lane in -1 -3 -4 -5 -6 -7; do
    run_driver rm_lane "$FIXTURES/e6mini.xodr" 0 "$lane" 100 -2 \
        || fail "rm_lane cross-lane $lane exited $?"
    need_kv pos_lane -2 "handle really is on lane -2, not on the queried lane"
    need_kv type_handle "${T[$lane]}" "RM_GetLaneType(handle on -2, $lane)"
done
