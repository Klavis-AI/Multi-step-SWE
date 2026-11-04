#!/usr/bin/env bash
# NEW. The distance-valued getters - odometer, acceleration, route length -
# return double. The odometer is the discriminating one: it is an accumulator,
# so a float version loses precision progressively rather than once.
source /tests/caselib.sh

run_driver se_motion "$FIXTURES/two_car_brake.xosc" 13.3333333 0.0 0.25 10 0.1
need_status 0 $? "se_motion"

need_kv init 0 "SE_Init"
need_kv stepped 10 "the driver completed all ten steps"

# Ego runs at 20 m/s and Target at 10 m/s for one second of scenario time.
# These are exact in both precisions; they are asserted so that a solution
# which widens the type but breaks the odometer is still caught.
need_close odo0 20 1e-12 "Ego odometer after one second"
need_close odo1 10 1e-12 "Target odometer after one second"

# Neither entity is accelerating during the reported interval, and neither has
# a route assigned, so both of these are exactly zero.
need_close acc0 0 1e-15 "Ego acceleration"
need_close routelen0 0 1e-15 "Ego has no route, so its route length is zero"

need_kv survived 1 "driver reached SE_Close without aborting"
