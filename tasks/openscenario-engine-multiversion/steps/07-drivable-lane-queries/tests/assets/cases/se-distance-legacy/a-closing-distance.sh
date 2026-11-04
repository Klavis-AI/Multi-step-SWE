#!/usr/bin/env bash
# Inherited. The legacy distance query and the scenario stepping around it must
# be unaffected by this step's lane-query rework.
source /tests/caselib.sh

run_driver se_dist_legacy "$FIXTURES/two_car_brake.xosc" 300 0.01
need_status 0 $? "se_dist_legacy"
need_kv n_objects 2 "objects in the scenario"

# Ego starts 100 m behind Target and closes at 10 m/s for 3 s.
need_close ds_t0 100.0 0.001 "distance at t=0"
need_close sim_time 3.0 0.001 "simulated time"
need_close ds_end 70.0 0.001 "distance at the end"
need_close obj0_speed 20.0 0.001 "ego speed"
need_close obj1_speed 10.0 0.001 "target speed"
need_close obj0_s 80.0 0.001 "ego road position"
need_close obj1_s 150.0 0.001 "target road position"

# The reported distance must be the two road positions, not an independently
# drifting number: a frozen or recomputed-from-nothing distance fails here.
gap=$(awk -v a="$(sed -n 's/^obj0_s=//p' "$WORK/last.out" | tail -1)" \
          -v b="$(sed -n 's/^obj1_s=//p' "$WORK/last.out" | tail -1)" \
          -v d="$(sed -n 's/^ds_end=//p' "$WORK/last.out" | tail -1)" \
          'BEGIN{ g=b-a; print (g-d<0.001 && d-g<0.001) ? "ok" : "bad("g" vs "d")" }')
[ "$gap" = ok ] || fail "reported distance disagrees with the road positions: $gap"

# instruction.md's return-convention note fixes the SIGN of a failing return and
# explicitly declines to fix its magnitude, so this asserts negativity only.
need_neg rc_bad "distance query against an object that does not exist"
need_kv survived 1 "driver completed"
exit 0
