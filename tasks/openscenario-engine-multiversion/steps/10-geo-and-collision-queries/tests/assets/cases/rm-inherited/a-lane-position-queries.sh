#!/usr/bin/env bash
# Regression floor. The road-manager library's core query set -- open a network,
# count its roads, measure a road, create a position handle, place it on a lane
# and read it back -- behaves identically at both ends of this step. Every value
# below was measured at both tags and is byte-identical there.
#
# Nothing in this case is new at this step. It exists so that a change made for
# the sake of the new geometry and collision queries cannot quietly break what
# already worked.
source /tests/caselib.sh

cp "$FIXTURES/straight_500m.xodr" "$WORK/"

# Right-hand driving lane, 100 m in.
run_driver rm_core "$WORK/straight_500m.xodr" 1 -1 100
need_status 0 $? "rm_core on straight_500m lane -1"
need_kv init 0 "RM_Init"
need_kv nroads 1 "RM_GetNumberOfRoads"
need_kv first_road_id 1 "RM_GetIdOfRoadFromIndex(0)"
need_kv road_length 500.000 "RM_GetRoadLength"
need_kv nlanes 7 "RM_GetRoadNumberOfLanes"
need_kv handle 0 "RM_CreatePosition"
need_kv set_lane_ok 1 "RM_SetLanePosition"
need_kv get_position 1 "RM_GetPositionData"
need_grep '^pos_road=1 pos_lane=-1 pos_s=100\.000$' "$WORK/last.out" "position reads back as it was set"
need_grep '^width_rc=1 width=3\.0700$' "$WORK/last.out" "RM_GetLaneWidth"
need_kv survived 1 "RM_Close"

# The mirrored driving lane on the other side of the centre line, at a
# different s. Same network, different answer for the position, same width.
run_driver rm_core "$WORK/straight_500m.xodr" 1 1 250
need_status 0 $? "rm_core on straight_500m lane 1"
need_kv init 0 "RM_Init on lane 1"
need_kv road_length 500.000 "RM_GetRoadLength on lane 1"
need_grep '^pos_road=1 pos_lane=1 pos_s=250\.000$' "$WORK/last.out" "left-hand lane position"
need_grep '^width_rc=1 width=3\.0700$' "$WORK/last.out" "left-hand lane width"
need_kv survived 1 "RM_Close on lane 1"
