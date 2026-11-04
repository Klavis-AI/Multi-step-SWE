#!/usr/bin/env bash
# INHERITED. The simple-vehicle model's deceleration limiter predates this step.
# Driving it at a 5 m/s^2 cap for 20 steps of 0.1 s from 30 m/s must produce a
# straight 0.5 m/s drop per step and land exactly on 20 m/s. Measured identical
# at v2.23.1 and v2.27.0.
source /tests/caselib.sh

run_driver se_simple_vehicle 5.0 20 0.1
need_status 0 $? "se_simple_vehicle"

need_kv create 1             "SE_SimpleVehicleCreate returned a vehicle"
need_kv speed_0 30.0000      "initial speed"

# Sampled rather than exhaustive: the endpoints plus enough interior points to
# pin the slope. A model that decelerates in one jump and then coasts would
# match the endpoints alone.
need_kv speed_1 29.5000      "speed after 1 step"
need_kv speed_5 27.5000      "speed after 5 steps"
need_kv speed_10 25.0000     "speed after 10 steps"
need_kv speed_15 22.5000     "speed after 15 steps"
need_kv speed_20 20.0000     "speed after 20 steps"
need_kv speed_final 20.0000  "final speed"

# The cap is the actual requirement: no single step may exceed it.
need_kv worst_drop_per_step 0.5000  "largest speed drop in any one step"
need_kv worst_decel 5.0000          "implied worst-case deceleration"
need_kv survived 1                  "driver reached the end without aborting"
