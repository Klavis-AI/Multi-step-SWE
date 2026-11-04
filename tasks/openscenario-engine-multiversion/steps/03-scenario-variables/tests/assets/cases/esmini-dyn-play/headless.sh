#!/usr/bin/env bash
# esmini-dyn is the player driven through the shared library rather than
# linked into the application. It takes the same options and plays the same
# scenario to the same stop trigger.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_tool "$ESMINI_DYN" --osc "$WORK/two_car_brake.xosc" --headless --fixed_timestep 0.05
need_status 0 $? "esmini-dyn headless run"
need_grep 'esmini BUILD VERSION' "$WORK/last.out" "esmini-dyn identifies itself"
need_grep 'Loading .*two_car_brake\.xosc' "$WORK/last.out" "esmini-dyn loaded the scenario"
need_grep '^Closing$' "$WORK/last.out" "esmini-dyn shut down cleanly"

# The application drives the library in a loop and queries a sensor each frame;
# a run that stopped at once would not reach a hundred of them.
frames=$(grep -c 'Detections \[sensor ID' "$WORK/last.out")
[ "$frames" -ge 100 ] || fail "expected the dyn player to step many frames, saw $frames"
