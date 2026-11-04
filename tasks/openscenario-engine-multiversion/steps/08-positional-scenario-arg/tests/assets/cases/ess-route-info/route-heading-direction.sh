#!/usr/bin/env bash
source /tests/caselib.sh

# The route lookahead respects the route all the way through the junction: at a
# 40 m lookahead each route sits on its own successor road, and the right-hand
# route reaches it on the far side of the junction.

run_driver se_route_info "$FIXTURES/junction_route_straight.xosc" 40
need_kv rc_straight 1 "straight plain lookahead return code"
need_kv rc_route 1 "straight route lookahead return code"
need_kv road_route 0 "straight route lookahead road"
need_kv same_road 1 "straight: route and plain agree"

run_driver se_route_info "$FIXTURES/junction_route_left.xosc" 40
need_kv road_route 1 "left route lookahead road"
need_kv same_road 0 "left: route diverges from plain"

run_driver se_route_info "$FIXTURES/junction_route_right.xosc" 40
need_kv road_route 3 "right route lookahead road"
need_kv lane_route 1 "right route lookahead lane"
need_kv survived 1 "driver reached the end"

# A non-zero return code is a success signal from the road manager, not an
# error. The magnitude is pinned, not just the sign: instruction.md requirement 7
# publishes the geometry that fixes it -- the entity stands 24.194 m short of the
# junction and the connecting roads are 15.475/14.865/9.243 m long, so a 30 m
# probe is still on the connecting road (code 2 = made a junction choice) while
# the 40 m probes above have left it for the road beyond (code 1 = entered a new
# road). rc_route_sign is kept as the coarser cross-check.
run_driver se_route_info "$FIXTURES/junction_route_left.xosc" 30
need_kv rc_straight 2 "positive plain return code"
need_kv rc_route 2 "positive route return code"
need_kv rc_route_sign 1 "route return code sign"
