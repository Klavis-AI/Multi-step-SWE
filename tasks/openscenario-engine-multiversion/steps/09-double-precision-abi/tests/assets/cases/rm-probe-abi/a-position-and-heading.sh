#!/usr/bin/env bash
# NEW. The road manager's position surface widens to double at the same time as
# the scenario engine's: RM_SetS, RM_SetH and RM_PositionData's fields all carry
# double, and a fractional s set through the API must read back undamaged.
source /tests/caselib.sh

run_driver rm_probe "$FIXTURES/straight_500m.xodr" 1 -1 123.456789 25.0 0.3
need_status 0 $? "rm_probe"

need_kv init 0      "RM_Init on a valid .xodr"
need_kv nroads 1    "straight_500m.xodr has one road"
need_kv handle_ok 1 "RM_CreatePosition returned a usable handle"

# The driver normalises each setter's return to 1 for any non-negative code.
# Position setters return 1 and 2 as well as 0 to mean success, so the sign is
# the contract and the exact code is not.
need_kv setroad 1 "RM_SetRoadPosition"
need_kv setlane 1 "RM_SetLanePosition"
need_kv sets 1    "RM_SetS"
need_kv getpos 1  "RM_GetPositionData"

# 123.456789 through a float loses about 6e-6 here; the double keeps it.
need_close pos_s 123.456789 1e-12 "s survives the set/get round trip in double precision"
need_close pos_x 123.456789 1e-12 "global x equals s on this straight road, to double precision"
need_kv pos_lane -1 "the position stayed on the lane it was set to"

need_kv seth 1 "RM_SetH"
need_close pos_h 0.3 1e-12 "heading survives the set/get round trip in double precision"

need_kv survived 1 "driver reached the end without aborting"
