#!/usr/bin/env bash
# The other half of the widening claim: the numbers must not move. rm_core is
# deliberately width-agnostic - it deduces the lane-width out-parameter's type
# from the declaration rather than spelling it - so it compiles against both
# headers and the answers it prints can be compared against the same constants
# before and after. Tolerances here are loose on purpose: these assertions are
# about the road manager still describing the same network, not about precision.
#
# The return codes are the point as well, and they are asserted RAW. The
# convention is `< 0` for an error and `>= 0` for a success, with 0, 1 and 2
# all successes, and it does not change in this step. On this network and from
# this start the lane setter answers a plain 0 - measured - and the
# discriminating half of the convention is the transition below: following the
# road on from this lane position crosses onto the junction's target road and
# the road manager answers 1, not 0. An implementation that collapsed the
# non-zero successes onto 0 - the narrowing the instruction's §Notes bullet
# warns against - answers 0 there, and this file requires 1.
#
# SCORING NOTE - this case is inherited regression, NOT discrimination. rm_core
# was built against both headers and run on this fixture at each tag: it exits
# 0 at v2.59.0 and at v3.0.4 alike. Only six keys move between the tags at all
# - road_length, pos_x, pos_y, cross_s, rt_s and speed_limit - and none moves
# by more than about 6e-7, while every one of them is asserted at 1e-5 or 1e-4.
# The keys carrying the tight tolerances (the widths, at 1e-6 and 1e-9) do not
# move at all, because 3.5 and 0.0 are exact in both widths.
#
# That is deliberate and it is not a defect to be fixed by tightening: the job
# of this file is to prove the road manager still describes the SAME network
# after the widening, which is a regression question. The discrimination its
# group is scored for lives in speed_limit.sh and width.sh, whose 1e-12 and
# 1e-9 assertions separate the tags and would also catch a solution that
# widened the declarations while leaving a float pipeline behind them. A later
# round should not read this file passing at v2.59.0 as a hole.
source /tests/caselib.sh

run_driver rm_core "$FIXTURES/fabriksgatan.xodr" 5 -1 1.0
need_status 0 $? "rm_core"
need_kv init 0 "RM_Init"
need_kv nroads 16 "road count"
need_kv road0_id 0 "first road id"
need_kv survived 1 "RM_Close"

# The setters answer non-negative on success, and the exact code is asserted.
# 0 is a plain success; 1, further down, is a success that landed on a
# different road than the one asked for. Both are successes under the
# convention, and neither is a failure.
need_kv handle_ok 1 "RM_CreatePosition"
need_kv setlane 0 "RM_SetLanePosition reports a plain success"
need_kv setlane_ok 1 "RM_SetLanePosition reports success"
need_kv getpos 1 "RM_GetPositionData reports success"
need_kv setxyh 1 "RM_SetWorldXYHPosition reports success"

# The network the road manager describes is unchanged by the widening.
need_kv ndrivable 1 "drivable lanes on road 5"
need_close road_length 14.7052255 1e-5 "RM_GetRoadLength"
need_close speed_limit 16.666666 1e-5 "RM_GetSpeedLimit"
need_kv pos_lane -1 "lane id after RM_SetLanePosition"
need_kv pos_road 5 "road id after RM_SetLanePosition"
need_close pos_s 1.0 1e-6 "s after RM_SetLanePosition"
need_close pos_x 31.8344663 1e-5 "world x of that lane position"
need_close pos_y 0.222819417 1e-5 "world y of that lane position"

# Following the road on from this position leaves road 5 and lands on the road
# the junction resolves it to. The road manager says how: raw 1 means the
# position moved onto a new road segment, and 1 is a success. A driver, or a
# solution, that treated only 0 as success would report the failure here.
need_kv cross_rc 1 "RM_PositionMoveForward reports a road transition"
need_kv cross_ok 1 "that transition is a success, not an error"
need_kv cross_road 0 "road the transition landed on"
need_close cross_s 0.2947745 1e-4 "s on the road the transition landed on"

# Feeding the reported world point back in re-derives the same road and lane.
need_kv rt_road 5 "road id after the world round trip"
need_kv rt_lane -1 "lane id after the world round trip"
need_close rt_s 1.0 1e-4 "s after the world round trip"

# Lane widths and types: same numbers, wider container.
need_kv w_rc 0 "RM_GetLaneWidth"
need_close width_handle 3.5 1e-6 "lane width by handle"
need_kv wbr_rc 0 "RM_GetLaneWidthByRoadId"
need_close width_by_road 3.5 1e-6 "lane width by road id"

# The centre lane is a zero-width reference lane, not an error.
need_kv wc_rc 0 "RM_GetLaneWidthByRoadId on the centre lane"
need_close width_centre 0.0 1e-9 "centre lane width"
need_kv type_centre 1 "centre lane type"
need_kv type_lane 2 "driving lane type"
