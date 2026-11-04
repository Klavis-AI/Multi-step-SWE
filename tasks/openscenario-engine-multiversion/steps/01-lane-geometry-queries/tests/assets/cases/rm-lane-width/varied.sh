#!/usr/bin/env bash
# The same two entry points across a road whose seven lanes have seven
# different widths. e6mini.xodr road 0 has a single lane section, so every
# width below holds at every s.
source /tests/caselib.sh

# lane id -> width, read out of the fixture's <width a="..."> entries.
declare -A W=([-1]=2.60 [-2]=3.65 [-3]=3.50 [-4]=3.90 [-5]=2.85 [-6]=1.50 [-7]=6.00)

for lane in -1 -2 -3 -4 -5 -6 -7; do
    run_driver rm_lane "$FIXTURES/e6mini.xodr" 0 "$lane" 100 || fail "rm_lane lane $lane exited $?"
    need_kv init 0 "RM_Init"
    need_close width_by_road "${W[$lane]}" 0.001 "RM_GetLaneWidthByRoadId(0,$lane,100)"
    need_close width_handle "${W[$lane]}" 0.001 "RM_GetLaneWidth(handle,$lane)"
done

# Seven distinct widths from one road is the point of this case: a stub that
# returns a single constant passes no more than one lane of it.
run_driver rm_lane "$FIXTURES/e6mini.xodr" 0 -2 400 || fail "rm_lane exited $?"
need_close width_by_road 3.65 0.001 "width is constant in s on a one-section road"

# The handle form takes the road and the s from the handle and the LANE from
# its argument, so one position can be asked about a lane it is not on. Every
# query above placed the handle on the lane it then asked about, which an
# implementation that ignored the lane argument entirely would survive. These
# ask from a single position parked on lane -1.
for lane in -2 -3 -4 -5 -6 -7; do
    run_driver rm_lane "$FIXTURES/e6mini.xodr" 0 "$lane" 100 -1 \
        || fail "rm_lane cross-lane $lane exited $?"
    need_kv place_lane -1 "handle parked on lane -1"
    need_kv pos_lane -1 "handle really is on lane -1, not on the queried lane"
    need_close width_handle "${W[$lane]}" 0.001 "RM_GetLaneWidth(handle on -1, $lane)"
    need_close width_by_road "${W[$lane]}" 0.001 "RM_GetLaneWidthByRoadId(0,$lane,100) still agrees"
done
