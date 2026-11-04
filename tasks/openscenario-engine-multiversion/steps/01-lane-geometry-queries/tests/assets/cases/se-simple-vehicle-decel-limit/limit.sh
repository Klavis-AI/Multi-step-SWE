#!/usr/bin/env bash
# SE_SimpleVehicleSetMaxDeceleration caps how fast full braking sheds speed.
# The driver starts a simple vehicle at 30 m/s, sets the limit, then applies a
# full brake for 20 steps of 0.05 s and reports the worst single-step drop.
source /tests/caselib.sh

run_driver se_simple_vehicle 2.0 20 0.05 || fail "se_simple_vehicle exited $?"
need_kv create 1 "SE_SimpleVehicleCreate"
need_close speed_0 30.0 0.01 "initial speed"
# 1 s of braking at 2 m/s^2 from 30 m/s.
need_close speed_final 28.0 0.05 "speed after 1 s at 2 m/s^2"
need_close worst_decel 2.0 0.05 "deceleration never exceeds the limit"
need_kv survived 1 "driver ran to the end"

# The standstill end of the same rule (instruction.md section 7): a braking
# step that would carry the speed below zero leaves it at exactly 0 and holds
# it there.
#
# 7 m/s^2 for 0.05 s sheds 0.35 per step, so from 30 m/s the speed is 0.25 at
# step 85 and the next step is the one that would cross: 0.25 - 0.35 = -0.10.
# The cap is chosen so the crossing is unambiguous rather than landing on zero
# exactly -- a rate that divides 30 evenly never asks the question this
# checks. Braking continues for another 34 steps after the crossing and the
# speed must still be 0, not negative.
run_driver se_simple_vehicle 7.0 120 0.05 || fail "se_simple_vehicle exited $?"
need_close speed_85 0.25 0.05 "speed one step before the zero crossing"
need_close speed_86 0.0  0.05 "the crossing step settles at standstill"
need_close speed_final 0.0 0.05 "34 further steps of full brake do not reverse it"
need_close worst_decel 7.0 0.05 "the limit is still the rate on the way down"
need_kv survived 1 "driver ran to the end"

# The ceiling is the braking rate itself, not a second cap layered over a rate
# the model already had (instruction.md section 7: "the limit is reached, not
# merely respected ... the same is true at every value of maxDeceleration a
# caller chooses"). The model the workspace ships brakes at 20 m/s^2, so a
# rate above 20 is where the two readings part company: written as
# min(limit, 20) the answer here would be the inherited 20*dt per step, and
# the instruction says it must be maxDeceleration*dt. 40 m/s^2 for 10 steps of
# 0.05 s sheds 2.0 per step, so 30 m/s becomes 10 and standstill is never
# reached -- the clamp does not mask the difference.
run_driver se_simple_vehicle 40.0 10 0.05 || fail "se_simple_vehicle exited $?"
need_close speed_0 30.0 0.01 "initial speed"
need_close speed_1 28.0 0.05 "the first step sheds 40*dt, above the inherited 20*dt"
need_close speed_final 10.0 0.05 "speed after 10 steps at 40 m/s^2"
need_close worst_decel 40.0 0.05 "the ceiling is the rate above the inherited rate too"
need_kv survived 1 "driver ran to the end"
