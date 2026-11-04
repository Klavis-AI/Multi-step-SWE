#!/usr/bin/env bash
# A lane position converted to world coordinates and back must land on the same
# road and lane.
#
# Every return code here is asserted as ">= 0", never "== 0": at this release
# the road manager reports 1 for "moved into a new road segment" and 2 for
# "moved into a junction and made a choice", and a check written as "== 0" is a
# latent failure at this step and at every later one.
source /tests/caselib.sh

run_driver rm_core "$FIXTURES/straight_500m.xodr" 1 -1 250 || fail "rm_core exited $?"
need_kv nroads 1 "RM_GetNumberOfRoads on straight_500m"
need_kv first_road_id 1 "RM_GetIdOfRoadFromIndex(0)"
need_close road_length 500.0 0.01 "RM_GetRoadLength"
need_kv nlanes 2 "RM_GetRoadNumberOfLanes"
need_kv set_lane_ok 1 "RM_SetLanePosition returned >= 0"
need_kv xyh_ok 1 "RM_SetWorldXYHPosition returned >= 0"
need_kv roundtrip_road 1 "road after the world-coordinate round trip"
need_kv roundtrip_lane -1 "lane after the world-coordinate round trip"

run_driver rm_core "$FIXTURES/e6mini.xodr" 0 -3 400 || fail "rm_core exited $?"
need_kv nlanes 6 "lane count on e6mini road 0"
need_kv roundtrip_lane -3 "lane after the round trip on e6mini"
