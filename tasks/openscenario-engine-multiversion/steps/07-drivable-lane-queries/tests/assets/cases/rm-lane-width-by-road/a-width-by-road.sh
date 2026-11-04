#!/usr/bin/env bash
# The width query must agree between its two addressing forms on a road whose
# lanes have very different widths, and it must never be callable before a
# network has been loaded.
source /tests/caselib.sh

# fabriksgatan road 0 at s=20: the driving lanes are 3.5 m, the shoulder strips
# beside them 0.3 m, and the sidewalks 2.0 m. A query that returned one width
# for the whole road, or that confused the sign of the lane, cannot satisfy all
# three numbers at once.
check_lane() {
    local lane="$1" want="$2"
    run_driver rm_lane_width "$FIXTURES/fabriksgatan.xodr" 0 "$lane" 20.0
    need_status 0 $? "rm_lane_width lane $lane"
    need_kv w_road_rc 0 "width status, lane $lane"
    need_close w_road "$want" 0.01 "lane $lane width by road id"
    need_kv w_handle_rc 0 "handle width status, lane $lane"
    need_close w_handle "$want" 0.01 "lane $lane width by handle"
}

check_lane 1 3.5
check_lane -1 3.5
check_lane 2 0.3
check_lane -2 0.3
check_lane 3 2.0
check_lane -3 2.0

# The reference line itself carries no width.
run_driver rm_lane_width "$FIXTURES/fabriksgatan.xodr" 0 0 20.0
need_status 0 $? "rm_lane_width on the reference line"
need_kv w_road_rc 0 "reference line width status"
need_close w_road 0.0 0.01 "reference line has no width"

# Nothing can be answered before a network exists.
run_driver rm_lane_width "$FIXTURES/straight_500m.xodr" 1 -1 250.0
need_status 0 $? "rm_lane_width"
need_neg pre_init_road_rc "query before the network was loaded"
need_kv pre_init_road_untouched 1 "out parameter before load"
need_neg pre_init_handle_rc "handle query before the network was loaded"
need_kv pre_init_handle_untouched 1 "handle out parameter before load"
need_kv survived 1 "driver completed"
exit 0
