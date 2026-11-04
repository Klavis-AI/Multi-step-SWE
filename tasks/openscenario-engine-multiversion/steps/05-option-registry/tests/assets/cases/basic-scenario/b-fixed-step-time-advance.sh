#!/usr/bin/env bash
# Inherited. SE_StepDT must advance simulation time deterministically
# regardless of how fast the caller runs, and the object's final speed must
# still be reported once the scenario has run to completion.
source /tests/caselib.sh

cp "$FIXTURES/elev_cruise.xosc" "$WORK/elev_cruise.xosc"
cp "$FIXTURES/curves_elevation.xodr" "$WORK/curves_elevation.xodr"

run_driver d_basic_scenario elev_cruise.xosc
need_status 0 $? "d_basic_scenario"

need_kv simtime 5.000 "100 steps of SE_StepDT(0.05) advance exactly 5 seconds"
need_kv x_known 1 "the object moved from its initial x"
need_kv speed_ge0 1 "reported speed is never negative"
need_close speed_final 10.000 0.01 "the object holds its constant cruise speed"
need_kv survived 1 "driver completed"
exit 0
