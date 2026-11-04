#!/usr/bin/env bash
# Placing a handle by road id, s and lateral offset t. The coordinates that come
# back must be the ones asked for, and every failure mode must be a status code
# rather than a silently wrong position.
source /tests/caselib.sh

ODR="$FIXTURES/straight_500m.xodr"

# align=1: heading is taken from the road's driving direction at that point.
run_driver rm_road_pos "$ODR" 1 250 -1.535 1
need_status 0 $? "rm_road_pos"
need_kv init 0 "road manager init"
need_kv handle_ok 1 "position handle created"

# The setter reports success with a non-negative code. It is deliberately NOT
# compared against 0: the road manager widened these setters to return 1 and 2
# as well as 0 in 2.21.2, and pinning success to exactly 0 is a regression that
# this assertion exists to catch.
need_kv set_ok 1 "set by road position succeeded (status >= 0)"
need_kv get_ok 1 "position read back"

# straight_500m is a 500 m straight drawn along +x, so s maps directly to x and
# t maps directly to y. Lane -1 is the right-hand driving lane whose centre sits
# at t = -1.535.
need_kv road 1 "road id"
need_kv lane -1 "lane id"
need_close s 250.000 0.001 "s coordinate"
need_close lane_offset 0.000 0.001 "lane offset"
need_close x 250.000 0.001 "world x"
need_close y -1.535 0.001 "world y"

# Failure modes are statuses, not positions.
need_neg bad_road_rc "undefined road id"
need_neg bad_handle_rc "handle that was never created"
need_kv survived 1 "driver completed"
exit 0
