#!/usr/bin/env bash
source /tests/caselib.sh

# A wider road reports several distinct lanes, each with its own width and type
# at the same station.

run_driver rm_lane "$FIXTURES/e6mini.xodr" 0 -2 100
need_kv init 0 "road manager init"
need_close width_by_road 3.65 0.001 "inner driving lane width"
need_kv type_by_road 2 "inner driving lane type"
need_close width_handle 3.65 0.001 "inner driving lane width by handle"
need_kv type_handle 2 "inner driving lane type by handle"
need_kv survived 1 "driver reached the end"

run_driver rm_lane "$FIXTURES/e6mini.xodr" 0 -1 100
need_close width_by_road 2.60 0.001 "outer lane width"
need_kv type_by_road 64 "outer lane is not a driving lane"
need_kv type_handle 64 "outer lane type agrees by handle"
need_kv survived 1 "driver reached the end"

# The outermost lane of the section is a STOP lane -- LANE_TYPE_STOP = 1 << 2 = 4,
# declared `type="stop"` on lane -5 of the fixture. It has a real width and a type
# of its own, so the query is reporting the lane's type and not a constant. (It is
# a member of LANE_TYPE_ANY_ROAD but NOT of LANE_TYPE_ANY_DRIVING; e6mini contains
# no shoulder lane at all.)
run_driver rm_lane "$FIXTURES/e6mini.xodr" 0 -5 100
need_close width_by_road 2.85 0.001 "stop-lane width"
need_kv type_by_road 4 "stop-lane type"
need_kv survived 1 "driver reached the end"
