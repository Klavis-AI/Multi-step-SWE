#!/usr/bin/env bash
# SE_AddObjectWithBoundingBox: the caller supplies the dimensions and the box
# centre, the call returns a usable object id, and the box that comes back out
# of SE_GetObjectState is the box that went in.
#
# Two different boxes are added in two runs. A library that hard-codes one
# shape, or that ignores the box it was handed, cannot produce both.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_bbox "$WORK/two_car_brake.xosc" 5.0 2.0 1.5 1.4 0.0 0.75 0
need_kv init 0 "SE_Init"
need_kv nobj_before 2 "the scenario's own two entities"
need_kv bb_add_ok 1 "SE_AddObjectWithBoundingBox returned a usable id"
need_kv nobj_after 4 "the two added objects are now counted"
need_kv bb_state_rc 0 "SE_GetObjectState on the added object"
need_close bb_length 5.0 0.001 "box length"
need_close bb_width 2.0 0.001 "box width"
need_close bb_height 1.5 0.001 "box height"
need_close bb_cx 1.4 0.001 "box centre x"
need_close bb_cy 0.0 0.001 "box centre y"
need_close bb_cz 0.75 0.001 "box centre z"
need_kv bb_name "bbox_obj" "the name the object was given"
need_kv survived 1 "driver ran to the end"

# Same call, a completely different box.
run_driver se_bbox "$WORK/two_car_brake.xosc" 12.5 3.25 4.0 -2.5 1.0 2.0 0
need_kv bb_add_ok 1 "SE_AddObjectWithBoundingBox with a second box"
need_close bb_length 12.5 0.001 "box length from the second call"
need_close bb_width 3.25 0.001 "box width from the second call"
need_close bb_height 4.0 0.001 "box height from the second call"
need_close bb_cx -2.5 0.001 "box centre x from the second call"
need_close bb_cy 1.0 0.001 "box centre y from the second call"
need_close bb_cz 2.0 0.001 "box centre z from the second call"
