#!/usr/bin/env bash
# The legacy distance query must describe the scenario's physical state, not
# the integrator's step count: the same 3 s of simulated time reached with
# half as many steps of twice the size must produce the same separation. This
# is the property that catches a distance accumulated per-step rather than
# computed from the current positions.
source /tests/caselib.sh

run_driver se_dist_legacy "$FIXTURES/two_car_brake.xosc" 300 0.01 || fail "se_dist_legacy (fine) exited $?"
cp "$WORK/last.out" "$WORK/fine.out"
need_kv survived 1 "fine-step run completed"
need_close sim_time 3.0 0.001 "fine-step simulated time"

run_driver se_dist_legacy "$FIXTURES/two_car_brake.xosc" 150 0.02 || fail "se_dist_legacy (coarse) exited $?"
cp "$WORK/last.out" "$WORK/coarse.out"
need_kv survived 1 "coarse-step run completed"
need_close sim_time 3.0 0.001 "coarse-step simulated time"
need_close ds_end 70.0 0.001 "coarse-step separation"

fine=$(sed -n 's/^ds_end=//p' "$WORK/fine.out" | tail -1)
coarse=$(sed -n 's/^ds_end=//p' "$WORK/coarse.out" | tail -1)
[ -n "$fine" ] && [ -n "$coarse" ] || fail "one of the runs printed no ds_end"
awk -v a="$fine" -v b="$coarse" 'BEGIN{d=a-b; if(d<0)d=-d; exit !(d<0.001)}' \
    || fail "separation depends on the timestep: 0.01 gave $fine, 0.02 gave $coarse"

# Half the elapsed time must leave the gap half-closed: the pair closes at a
# constant 10 m/s, so 1 s of simulation leaves 90 m rather than 70 m. Without
# this a query that returned the initial 100 m forever would satisfy the
# invariance check above.
run_driver se_dist_legacy "$FIXTURES/two_car_brake.xosc" 100 0.01 || fail "se_dist_legacy (short) exited $?"
need_kv survived 1 "short run completed"
need_close sim_time 1.0 0.001 "short run simulated time"
need_close ds_end 90.0 0.001 "separation after 1 s"
