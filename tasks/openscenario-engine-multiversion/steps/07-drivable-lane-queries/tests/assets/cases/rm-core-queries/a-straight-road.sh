#!/usr/bin/env bash
# Inherited. The core road-manager queries this step does not touch must keep
# working exactly as before: path-based init, road count, road length, lane type
# by handle and by road id, and overlap enumeration. Every value here was
# measured identical at both ends of this step.
source /tests/caselib.sh

ODR="$FIXTURES/straight_500m.xodr"

run_driver rm_core_inherited "$ODR" 1 -1 250
need_status 0 $? "rm_core_inherited"
need_kv init 0 "path-based road manager init"
need_kv nroads 1 "number of roads"
need_kv first_road_id 1 "first road id"
need_close road_length 500.000 0.01 "road length"

# Lane type 2 is the driving lane type. Both addressing forms must agree.
need_kv lane_type 2 "lane type by handle"
need_kv lane_type_by_road 2 "lane type by road id"

# A lone straight road overlaps nothing.
need_kv n_overlap 0 "number of overlapping roads"
need_kv overlap_0 -1 "overlap index 0 on a road with no overlaps"
need_kv survived 1 "driver completed"
exit 0
