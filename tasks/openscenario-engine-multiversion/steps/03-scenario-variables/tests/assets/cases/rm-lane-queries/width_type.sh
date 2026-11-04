#!/usr/bin/env bash
# Road-manager lane geometry, established by an earlier step and unchanged
# here. Both the handle form and the road-id form are asked, and the widths
# and types come from the fixture rather than from the lane id.
source /tests/caselib.sh

run_driver rm_lane "$FIXTURES/straight_500m.xodr" 1 -1 250 || fail "rm_lane exited $?"
need_kv init 0 "RM_Init"
need_kv nroads 1 "RM_GetNumberOfRoads on straight_500m"
# WORKSPACE_HANDOFF.md pins this figure ("still answering 2 for it exactly as
# the earlier step recorded"), so it is asserted rather than left to the
# driver's printout.
need_kv nlanes 2 "RM_GetRoadNumberOfLanes on straight_500m at s=250"
need_close width_by_road 3.07 0.001 "RM_GetLaneWidthByRoadId(1,-1,250)"
need_close width_handle 3.07 0.001 "RM_GetLaneWidth(handle,-1)"
need_kv type_by_road 2 "RM_GetLaneTypeByRoadId on a driving lane"
need_kv type_handle 2 "RM_GetLaneType on a driving lane"

run_driver rm_lane "$FIXTURES/straight_500m.xodr" 1 0 250 || fail "rm_lane exited $?"
need_close width_by_road 0.0 0.001 "the centre lane has width 0"
need_kv type_by_road 1 "the centre lane has type NONE"

# Seven lanes with seven widths and three types on one road: a constant does
# not survive this.
declare -A W=([-1]=2.60 [-2]=3.65 [-3]=3.50 [-4]=3.90 [-5]=2.85 [-6]=1.50 [-7]=6.00)
declare -A T=([-1]=64 [-2]=2 [-3]=2 [-4]=2 [-5]=4 [-6]=64 [-7]=64)
for lane in -1 -2 -3 -4 -5 -6 -7; do
    run_driver rm_lane "$FIXTURES/e6mini.xodr" 0 "$lane" 100 || fail "rm_lane lane $lane exited $?"
    need_kv nlanes 6 "RM_GetRoadNumberOfLanes on e6mini at s=100"
    need_close width_by_road "${W[$lane]}" 0.001 "RM_GetLaneWidthByRoadId(0,$lane,100)"
    need_close width_handle "${W[$lane]}" 0.001 "RM_GetLaneWidth(handle,$lane)"
    need_kv type_by_road "${T[$lane]}" "RM_GetLaneTypeByRoadId(0,$lane,100)"
    need_kv type_handle "${T[$lane]}" "RM_GetLaneType(handle,$lane)"
done
