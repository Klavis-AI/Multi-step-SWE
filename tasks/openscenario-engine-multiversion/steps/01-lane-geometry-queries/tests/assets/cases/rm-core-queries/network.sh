#!/usr/bin/env bash
# Road-manager entry points that already existed at the starting release. This
# is a regression group: it must keep passing while the new queries are added.
source /tests/caselib.sh

run_driver rm_core "$FIXTURES/fabriksgatan.xodr" 0 -1 20 || fail "rm_core exited $?"
need_kv init 0 "RM_Init"
need_kv nroads 16 "RM_GetNumberOfRoads on fabriksgatan"
need_kv all_roads_have_length 1 "every road index maps to an id with a positive length"
need_kv get_position 1 "RM_GetPositionData"
need_kv pos_road 0 "road after RM_SetLanePosition"
need_kv pos_lane -1 "lane after RM_SetLanePosition"
need_close pos_s 20.0 0.001 "s after RM_SetLanePosition"
need_kv survived 1 "driver ran to the end"
