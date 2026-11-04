#!/usr/bin/env bash
# Inherited, on a road that is neither straight nor isolated, so the same
# queries are exercised where the answers are not trivially zero.
source /tests/caselib.sh

ODR="$FIXTURES/e6mini.xodr"

run_driver rm_core_inherited "$ODR" 0 -3 100
need_status 0 $? "rm_core_inherited"
need_kv init 0 "path-based road manager init"
need_kv first_road_id 0 "first road id"
need_close road_length 1464.434 0.01 "curved road length"
need_kv lane_type 2 "lane type by handle"
need_kv lane_type_by_road 2 "lane type by road id"

# This network has one road whose surface overlaps itself at the query point, so
# the overlap enumeration returns a real road id rather than the empty-case
# sentinel.
need_kv n_overlap 1 "number of overlapping roads"
need_kv overlap_0 0 "first overlapping road id"
need_kv survived 1 "driver completed"
exit 0
