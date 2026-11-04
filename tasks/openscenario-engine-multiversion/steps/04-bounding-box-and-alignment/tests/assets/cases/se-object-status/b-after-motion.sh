#!/usr/bin/env bash
# INHERITED. Same surface, three fixed timesteps later: both entities have
# moved by their own constant speed and the storyboard's Story-level Act has
# begun. Measured identical at v2.27.5 and v2.31.10.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_status "$WORK/two_car_brake.xosc" 3
need_status 0 $? "se_status on two_car_brake.xosc, 3 steps"

need_kv init 0    "SE_Init"
need_kv nobj 2     "still two entities"
need_kv quit 0      "the stop trigger has not fired yet at 0.15s"

need_kv s_0 23.000    "Ego advanced 3 m in 3 steps of 0.05s at 20 m/s"
need_kv speed_0 20.000 "Ego's speed is unchanged (no deceleration yet)"
need_kv road_0 1        "Ego stays on the same road"
need_kv lane_0 -1         "Ego stays in the same lane"

need_kv s_1 121.500        "Target advanced 1.5 m in 3 steps of 0.05s at 10 m/s"
need_kv speed_1 10.000       "Target's speed is unchanged"

need_neg bad_state_rc    "an out-of-range id is still rejected after stepping"
need_kv survived 1        "driver ran to the end"
