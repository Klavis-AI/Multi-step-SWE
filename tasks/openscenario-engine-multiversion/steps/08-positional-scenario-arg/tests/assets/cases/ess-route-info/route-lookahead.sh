#!/usr/bin/env bash
source /tests/caselib.sh

# A route-aware lookahead: past a junction, SE_GetRoadInfoAlongRoute follows the
# route the scenario assigned while SE_GetRoadInfoAtDistance follows the road
# manager's own continuation. At lookahead 30 m the two disagree, and each
# route reaches a different successor road.

run_driver se_route_info "$FIXTURES/junction_route_straight.xosc" 30
need_kv init 0 "straight init"
need_kv rc_straight 2 "straight plain lookahead return code"
need_kv road_straight 14 "straight plain lookahead road"
need_kv rc_route 2 "straight route lookahead return code"
need_kv road_route 14 "straight route lookahead road"
need_kv same_road 1 "straight: route and plain agree"

run_driver se_route_info "$FIXTURES/junction_route_left.xosc" 30
need_kv road_straight 14 "left plain lookahead road"
need_kv road_route 15 "left route lookahead road"
need_kv same_road 0 "left: route diverges from plain"

run_driver se_route_info "$FIXTURES/junction_route_right.xosc" 30
need_kv road_straight 14 "right plain lookahead road"
need_kv road_route 16 "right route lookahead road"
need_kv same_road 0 "right: route diverges from plain"
need_kv survived 1 "driver reached the end"

# ---------------------------------------------------------------------------
# Two road-manager behaviours that changed underneath the lookahead, both
# observed with a driver that compiles unchanged at BOTH ends of this step:
# the separation below is in the values, not in whether the driver builds.
# ---------------------------------------------------------------------------

# (a) A lookahead that stays on the road is unremarkable and must remain so.
# Road 1 of the flat fixture is 500 m long; starting at s = 250 and looking
# 200 m ahead lands at s = 450, still on the road, and reports success.
# "Success" is NON-NEGATIVE, not zero: instruction.md and WORKSPACE_HANDOFF.md
# both state that the position setters and BOTH lookaheads report 1 and 2
# alongside 0, so the three return codes here are asserted by sign only. The
# substance of the check is the road, the lane and the s below.
run_driver rm_lookahead "$FIXTURES/straight_500m.xodr" 1 -1 250 200
need_kv init 0 "rm_lookahead init"
need_nonneg set_lane_rc "position placed on road 1 lane -1"
need_nonneg probe_rc "a lookahead that stays on the road succeeds"
need_kv probe_road 1 "probe stayed on road 1"
need_kv probe_lane -1 "probe stayed in lane -1"
need_close probe_s 450.0 0.01 "probe landed 200 m further along"
need_nonneg lane_rc "the lane-info lookahead agrees"
need_close lane_s 450.0 0.01 "lane info landed at the same s"
need_kv survived 1 "rm_lookahead reached the end"

# (b) The same call with a lookahead that runs off the end of the road. The
# road does not continue - it has no successor - so the lookahead cannot
# deliver what was asked for, and both lookahead entry points have to say so
# in their return code rather than quietly clamping and reporting success.
# ERROR_END_OF_ROAD is -2; only the sign is pinned, because the documents
# promise a negative code and not a particular one.
run_driver rm_lookahead "$FIXTURES/straight_500m.xodr" 1 -1 250 300
need_kv init 0 "rm_lookahead init, overlong lookahead"
need_neg probe_rc "probe lookahead past the end of the road reports an error"
need_neg lane_rc "lane-info lookahead past the end of the road reports an error"
# The s it did reach is still filled in, clamped to the end of the road, so
# the error code is additional information and not a refusal to answer.
need_close probe_s 500.0 0.01 "probe clamped to the end of the road"
need_close lane_s 500.0 0.01 "lane info clamped to the end of the road"
need_kv survived 1 "rm_lookahead reached the end, overlong lookahead"

# (c) Superelevation. banked_500m.xodr is straight_500m.xodr with a single
# <superelevation a="0.1"> element added and nothing else changed, so the two
# runs below differ by the banking alone. Lane -1 is 3.07 m wide with its
# centre 1.535 m from the reference line MEASURED ALONG THE ROAD SURFACE. If
# the road is rotated about the reference line by 0.1 rad, the lane centre's
# horizontal offset shrinks to 1.535*cos(0.1) = 1.52733 and its height drops
# to -1.535*sin(0.1) = -0.15324. If only the road edges are lifted, the
# horizontal offset stays at the flat 1.535 and only z moves.
run_driver rm_lookahead "$FIXTURES/straight_500m.xodr" 1 -1 250 200
need_close pos_y -1.535 0.0005 "flat road: lane centre at the full lane offset"
need_close pos_z 0.0 0.0005 "flat road: lane centre at zero height"

run_driver rm_lookahead "$FIXTURES/banked_500m.xodr" 1 -1 250 200
need_kv init 0 "rm_lookahead init on the banked road"
need_close pos_x 250.0 0.0005 "banked road: s is unaffected"
need_close pos_y -1.527331 0.0005 "banked road: lane centre is rotated, not just raised"
need_close pos_z -0.153244 0.0005 "banked road: lane centre height follows the banking"
need_close pos_r 0.1 0.0005 "banked road: roll equals the superelevation"
need_kv survived 1 "rm_lookahead reached the end on the banked road"
