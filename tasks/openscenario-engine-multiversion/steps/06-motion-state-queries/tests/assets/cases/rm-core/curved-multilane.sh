#!/usr/bin/env bash
# The same inherited road-manager surface against a curved six-lane network,
# so the case does not pass merely because a degenerate implementation happens
# to be right on a straight road with the default lane.
source /tests/caselib.sh

run_driver rm_core "$FIXTURES/e6mini.xodr" 0 -3 100 || fail "rm_core exited $?"
need_kv survived 1 "driver ran to completion"
need_kv init 0 "RM_Init accepted the OpenDRIVE file"

need_kv nroads 1 "road count"
need_kv first_road_id 0 "id of the only road"
need_kv all_roads_have_length 1 "every road index maps to an id with positive length"
need_close road_length 1464.434 0.01 "road length"
need_kv nlanes 6 "driving lane count at s=100"

need_kv set_lane_ok 1 "RM_SetLanePosition succeeded"
need_kv get_position 1 "RM_GetPositionData succeeded"
need_kv pos_road 0 "position road id"
need_kv pos_lane -3 "position lane id"
need_close pos_s 100.000 0.001 "position s"

# Lane -3 is an outer lane, so a round trip that quietly snapped to the
# centre-most lane would be visible here.
need_kv xyh_ok 1 "RM_SetWorldXYHPosition succeeded"
need_kv roundtrip_road 0 "world coordinates map back to the same road"
need_kv roundtrip_lane -3 "world coordinates map back to the same outer lane"
