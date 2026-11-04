#!/usr/bin/env bash
source /tests/caselib.sh

# A lane position set explicitly, read back, and re-derived from its own world
# coordinates must land on the same road, lane and station. Both the explicit
# road id and the road manager's own index must refer to the same road.

run_driver rm_core "$FIXTURES/straight_500m.xodr" 1 -1 250
need_kv init 0 "road manager init"
need_kv nroads 1 "road count"
need_kv first_road_id 1 "first road id"
need_close road_length 500.0 0.01 "road length"
need_kv handle 0 "position handle"
need_kv set_lane_ok 1 "lane position accepted"
need_kv get_position 1 "position data readable"
need_kv pos_road 1 "position road"
need_kv pos_lane -1 "position lane"
need_close pos_s 250.0 0.01 "position station"
need_kv xyh_ok 1 "world position accepted"
need_kv roundtrip_road 1 "round-trip road"
need_kv roundtrip_lane -1 "round-trip lane"
need_close roundtrip_s 250.0 0.01 "round-trip station"
need_kv survived 1 "driver reached the end"

run_driver rm_core "$FIXTURES/e6mini.xodr" 0 -3 100
need_kv init 0 "e6mini road manager init"
need_kv nroads 1 "e6mini road count"
need_kv first_road_id 0 "e6mini first road id"
need_kv all_roads_have_length 1 "e6mini road has a length"
need_close road_length 1464.434 0.01 "e6mini road length"
need_kv ndrivable 6 "e6mini drivable lanes"
need_kv pos_road 0 "e6mini position road"
need_kv pos_lane -3 "e6mini position lane"
need_kv roundtrip_lane -3 "e6mini round-trip lane"
need_kv survived 1 "driver reached the end"
