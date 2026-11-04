#!/usr/bin/env bash
# INHERITED. Object enumeration and SE_ScenarioObjectState readback predate
# this step and must keep reporting identical fields for identical input.
# Measured identical at v2.27.5 and v2.31.10, taken at the moment the
# scenario's Init actions have placed both entities but before any Story
# maneuver has run.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_status "$WORK/two_car_brake.xosc" 0
need_status 0 $? "se_status on two_car_brake.xosc, 0 steps"

need_kv init 0       "SE_Init"
need_kv nobj 2        "the scenario declares two entities"
need_kv quit 0        "the stop trigger has not fired yet"

need_kv name_0 "Ego"          "first entity's name"
need_kv id_by_name_0 0         "SE_GetIdByName agrees with enumeration order"
need_kv state_rc_0 0            "SE_GetObjectState on the first entity"
need_kv obj_id_0 0               "the state's own id field"
need_kv road_0 1                  "Ego's road"
need_kv lane_0 -1                  "Ego's lane"
need_kv s_0 20.000                  "Ego's s at t=0"
need_kv speed_0 20.000                "Ego's initial speed"
need_kv length_0 5.000                 "Ego's bounding-box length"
need_kv width_0 2.000                   "Ego's bounding-box width"
need_kv height_0 1.500                   "Ego's bounding-box height"

need_kv name_1 "Target"        "second entity's name"
need_kv id_by_name_1 1           "SE_GetIdByName for the second entity"
need_kv state_rc_1 0               "SE_GetObjectState on the second entity"
need_kv road_1 1                     "Target's road"
need_kv lane_1 -1                     "Target's lane"
need_kv s_1 120.000                    "Target's s at t=0"
need_kv speed_1 10.000                   "Target's initial speed"

# Road and junction identity, both on the object's own state and on the
# SE_RoadInfo the engine fills in for a point ahead of it. Both structs declare
# roadId and junctionId as plain ints, and junctionId is -1 for a position that
# is not inside a junction -- which, on a single straight road, it never is.
# straight_500m.xodr has no junction at all, so -1 here is the whole contract.
need_kv junction_0 -1   "Ego is not in a junction"
need_kv junction_1 -1   "Target is not in a junction"
need_kv ri_rc 0          "SE_GetRoadInfoAtDistance 10 m ahead of Ego"
need_kv ri_road 1         "the looked-up point is on the same road"
need_kv ri_junction -1     "and carries -1 for its junction id, not 0 and not a road id"
need_kv ri_lane -1          "and stays in Ego's lane"
need_close ri_s 30.0 0.001   "10 m ahead of Ego's s=20"

need_neg bad_state_rc    "an out-of-range id is rejected, not silently accepted"
need_neg bad_id_by_name   "an unknown name resolves to no id"
need_kv survived 1        "driver ran to the end"
