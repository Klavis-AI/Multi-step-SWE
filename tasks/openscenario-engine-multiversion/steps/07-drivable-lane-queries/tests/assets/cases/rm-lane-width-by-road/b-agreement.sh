#!/usr/bin/env bash
# Widths read by road id must agree with widths read by handle, on a road whose
# lanes are nearly as wide as they are wider than the shoulder beside them. The
# two forms are the same query reached two ways; a case that only ever called
# one of them would not notice if they diverged.
source /tests/caselib.sh

ODR="$FIXTURES/straight_500m.xodr"

check_lane() {
    local lane="$1" want="$2"
    run_driver rm_lane_width "$ODR" 1 "$lane" 250.0
    need_status 0 $? "rm_lane_width lane $lane"
    need_kv w_road_rc 0 "width by road id, lane $lane"
    need_close w_road "$want" 0.01 "lane $lane width by road id"
    need_kv w_handle_rc 0 "width by handle, lane $lane"
    need_close w_handle "$want" 0.01 "lane $lane width by handle"
}

# straight_500m road 1: three lanes each side of the reference line. The middle
# lane each side is 1.68 m, the outer 6.0 m, the inner driving lane 3.07 m.
check_lane 1 3.07
check_lane -1 3.07
check_lane 2 1.68
check_lane -2 1.68
check_lane 3 6.0
check_lane -3 6.0

# A lane id the section does not carry has no width, and the query reports that
# as a width of zero rather than inventing a neighbouring lane's width. The
# reference-line id behaves the same way.
for absent in 0 -9; do
    run_driver rm_lane_width "$ODR" 1 "$absent" 250.0
    need_status 0 $? "rm_lane_width on lane $absent"
    need_kv w_road_rc 0 "width status, lane $absent"
    need_close w_road 0.0 0.0001 "lane $absent carries no width"
    need_close w_handle 0.0 0.0001 "lane $absent carries no width by handle"
done
need_kv survived 1 "driver completed"
exit 0
