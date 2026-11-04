#!/usr/bin/env bash
# The lane-width query reports its result through a caller-supplied out
# parameter and returns a status code. The same answer is reachable both from
# a position handle and from a road id plus s coordinate, and both forms
# report a status for every failure mode instead of a width.
source /tests/caselib.sh

ODR="$FIXTURES/straight_500m.xodr"

# Lane -1 of road 1 is a 3.07 m driving lane (the right-hand lane, since this
# road is drawn right-to-left about its reference line).
run_driver rm_lane_width "$ODR" 1 -1 250.0
need_status 0 $? "rm_lane_width"
need_kv init 0 "init"
need_kv w_road_rc 0 "width by road id status"
need_close w_road 3.07 0.01 "lane -1 width by road id"
need_kv w_handle_rc 0 "width by handle status"
need_close w_handle 3.07 0.01 "lane -1 width by handle"

# The two addressing forms must agree: a caller that has a handle and a caller
# that knows the road must see the same road.
need_kv set_lane_ok 1 "positioning on the lane"
need_kv survived 1 "driver completed"

# Every failure mode is a status code and leaves the caller's storage alone.
# A garbage width would be the classic way to get this wrong. Only the SIGN of
# the status is asserted - the documents promise a negative failure, not a
# particular one - while the untouched-storage half is exact.
need_neg bad_road_rc "undefined road id"
need_kv bad_road_untouched 1 "out parameter after undefined road id"
need_neg bad_handle_rc "handle that was never created"
need_kv bad_handle_untouched 1 "out parameter after bad handle"
need_neg null_dest_rc "null destination pointer"
need_neg post_close_rc "query after the network was closed"
need_kv post_close_untouched 1 "out parameter after close"

# The null-destination and post-close paths again, this time through the handle
# form itself. Requirement 6 names them for RM_GetLaneWidth, so checking them
# only on the road-id sibling would leave the function this group is named for
# untested on two of its three stated failure modes.
need_neg null_dest_handle_rc "null destination pointer, handle form"
need_neg post_close_handle_rc "handle query after the network was closed"
need_kv post_close_handle_untouched 1 "handle out parameter after close"
exit 0
