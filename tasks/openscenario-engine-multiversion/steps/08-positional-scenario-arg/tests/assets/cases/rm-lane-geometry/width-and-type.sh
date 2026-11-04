#!/usr/bin/env bash
source /tests/caselib.sh

# Lane width and lane type are queryable by explicit (road, lane, s) and again
# through a position handle placed at the same spot. Both routes must agree.

run_driver rm_lane "$FIXTURES/straight_500m.xodr" 1 -1 250
need_kv init 0 "road manager init"
need_kv width_by_road_rc 0 "width by road return code"
need_close width_by_road 3.07 0.001 "driving lane width by road"
need_kv type_by_road 2 "driving lane type by road"
need_kv set_lane_ok 1 "lane position accepted"
need_kv width_handle_rc 0 "width by handle return code"
need_close width_handle 3.07 0.001 "driving lane width by handle"
need_kv type_handle 2 "driving lane type by handle"
need_kv survived 1 "driver reached the end"

# A lane of type "none" is reported as such, with zero width, and the two
# lookup routes still agree.
run_driver rm_lane "$FIXTURES/straight_500m.xodr" 1 0 250
need_close width_by_road 0.0 0.0001 "centre lane width by road"
need_kv type_by_road 1 "centre lane type by road"
need_close width_handle 0.0 0.0001 "centre lane width by handle"
need_kv type_handle 1 "centre lane type by handle"
need_kv survived 1 "driver reached the end"
