#!/usr/bin/env bash
# INHERITED. Lane width and lane type, reached both by road id and through a
# position handle, must agree with each other and must not move across this
# step. Measured identical at v2.23.1 and v2.27.0.
source /tests/caselib.sh

run_driver rm_lane "$FIXTURES/straight_500m.xodr" 1 -1 250
need_status 0 $? "rm_lane on straight_500m.xodr"

need_kv init 0             "RM_Init on a valid .xodr"
need_kv nroads 1           "one road"
need_kv nlanes 2           "lane count at s=250"

need_kv width_by_road 3.0700  "RM_GetLaneWidthByRoadId"
need_kv type_by_road 2        "RM_GetLaneTypeByRoadId reports a driving lane"

# The handle-addressed spellings must return the same physical quantities as
# the road-id spellings; a solution where the two disagree has one of them wrong.
need_kv width_handle 3.0700   "RM_GetLaneWidth through a position handle"
need_kv type_handle 2         "RM_GetLaneType through a position handle"

need_kv pos_road 1         "road id read back"
need_kv pos_lane -1        "lane id read back"
need_kv pos_s 250.000      "s read back"

# RM_SetLanePosition's success codes were widened upstream (2.21.2) so that 1
# and 2 also mean success; only a negative value is a failure. Assert the sign,
# never the exact code, or a correct solution that returns 1 is failed here.
set_rc=$(sed -n 's/^set_lane=//p' "$WORK/last.out" | tail -1)
[ -n "$set_rc" ] || fail "driver printed no set_lane= line"
[ "$set_rc" -ge 0 ] || fail "RM_SetLanePosition returned $set_rc; a non-negative code is required"
