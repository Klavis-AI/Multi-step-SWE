#!/usr/bin/env bash
source /tests/caselib.sh

# Scenario lifecycle, the externally reported wheel state, the pause query and
# the simple-vehicle deceleration ceiling: four obligations an earlier step
# established, none of which move at this release. Two distinct answers from
# each of the two-valued entry points (wheel status, pause) rule out a stub
# that returns one constant.

run_driver se_state "$FIXTURES/two_car_brake.xosc" 2.0
need_kv pause_before -1 "SE_GetPauseFlag before SE_Init"
need_kv wheel_before -1 "SE_ReportObjectWheelStatus before SE_Init"
need_kv init 0 "SE_Init"
need_kv nobjects 2 "two entities loaded"
need_close sim_time 4.000 0.001 "80 steps of 0.05s"
need_kv pause_after 0 "SE_GetPauseFlag on a stepped scenario"
need_kv quit_after 0 "scenario has not reached its stop trigger yet"
need_kv get_state 0 "SE_GetObjectState"
need_kv obj0_id 0 "object 0 id"
need_close obj0_speed 17.15 0.05 "ego speed after braking"
need_kv wheel_valid 0 "SE_ReportObjectWheelStatus for a live object"
need_kv wheel_invalid -1 "SE_ReportObjectWheelStatus for an unknown object id"

need_kv create 1 "SE_SimpleVehicleCreate"
need_close speed_0 30.0 0.01 "initial speed"
# 1 s of braking at 2 m/s^2 from 30 m/s.
need_close speed_final 28.0 0.05 "speed after 1 s at 2 m/s^2"
need_close worst_decel 2.0 0.05 "deceleration never exceeds the limit"
need_kv survived 1 "driver reached the end"
