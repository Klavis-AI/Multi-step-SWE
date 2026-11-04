#!/usr/bin/env bash
# INHERITED. Same surface against fabriksgatan.xodr, a 16-road urban network
# with junctions, where the road count is the discriminator that a network was
# actually parsed rather than partially loaded. Measured identical at v2.23.1
# and v2.27.0.
source /tests/caselib.sh

run_driver rm_lane "$FIXTURES/fabriksgatan.xodr" 0 -1 10
need_status 0 $? "rm_lane on fabriksgatan.xodr"

need_kv init 0          "RM_Init on a valid .xodr"
need_kv nroads 16       "fabriksgatan.xodr has 16 roads"
need_kv nlanes 2        "lane count on road 0 at s=10"

need_kv width_by_road 3.5000  "RM_GetLaneWidthByRoadId"
need_kv type_by_road 2        "RM_GetLaneTypeByRoadId reports a driving lane"
need_kv width_handle 3.5000   "RM_GetLaneWidth through a position handle"
need_kv type_handle 2         "RM_GetLaneType through a position handle"

need_kv pos_road 0      "road id read back"
need_kv pos_lane -1     "lane id read back"
need_kv pos_s 10.000    "s read back"

set_rc=$(sed -n 's/^set_lane=//p' "$WORK/last.out" | tail -1)
[ -n "$set_rc" ] || fail "driver printed no set_lane= line"
[ "$set_rc" -ge 0 ] || fail "RM_SetLanePosition returned $set_rc; a non-negative code is required"
