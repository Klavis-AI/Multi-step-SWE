#!/usr/bin/env bash
# Commanded acceleration must integrate into speed linearly over the step. The
# per-step samples are checked, not just the endpoint, so a model that jumps to
# the final speed immediately or that applies the acceleration once cannot pass.
source /tests/caselib.sh

# a = 2.0 m/s^2, steering 0, 10 steps of 0.1 s, starting from 10 m/s.
run_driver se_acc_steer 2.0 0.0 10 0.1
need_status 0 $? "se_acc_steer"
need_kv create 1 "simple vehicle created"
need_close speed_0 10.0 0.001 "initial speed"

# Each 0.1 s step at 2.0 m/s^2 adds exactly 0.2 m/s.
need_close speed_1  10.2 0.001 "speed after 1 step"
need_close speed_2  10.4 0.001 "speed after 2 steps"
need_close speed_3  10.6 0.001 "speed after 3 steps"
need_close speed_5  11.0 0.001 "speed after 5 steps"
need_close speed_8  11.6 0.001 "speed after 8 steps"
need_close speed_10 12.0 0.001 "speed after 10 steps"
need_close speed_final 12.0 0.001 "final speed"

# Zero commanded steering must leave the heading and the lateral position alone.
need_close h_final 0.0 0.0001 "heading after straight-line control"
need_close y_final 0.0 0.001 "lateral position after straight-line control"

# Distance travelled is the integral of the ramp: 10*1.0 + 0.5*2.0*1.0^2 = 11.0
# by the continuous solution.
#
# Requirement 11 states the speed law and nothing about the position update, so
# the tolerance has to cover the integration scheme. The three ordinary schemes
# bracket the continuous answer rather than sitting above it: advancing the
# position with the POST-step speed gives 0.1*(10.2+...+12.0) = 11.1, which is
# what upstream prints; advancing it with the PRE-step speed - plain explicit
# Euler - gives 0.1*(10.0+...+11.8) = 10.9; the continuous solution is 11.0.
# The window is therefore centred on 11.0 and wide enough to hold all three,
# and it is still far from the 10.0 that a model ignoring the commanded
# acceleration entirely would report (1.0 m away, with speed_final 2.0 m/s
# away). The heading and lateral assertions above stay tight, because with zero
# steering every scheme holds them at exactly zero.
need_close x_final 11.0 0.15 "longitudinal position after the ramp"
need_kv survived 1 "driver completed"
exit 0
