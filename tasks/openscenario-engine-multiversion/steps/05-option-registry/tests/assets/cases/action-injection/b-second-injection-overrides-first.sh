#!/usr/bin/env bash
# New. Injecting a second speed action while the first is still transitioning
# must cancel the first: the object must settle on the second action's target,
# never the first one's, and the second injection must itself report ongoing.
source /tests/caselib.sh

cp "$FIXTURES/inject_cruise.xosc" "$WORK/inject_cruise.xosc"
cp "$FIXTURES/curves_elevation.xodr" "$WORK/curves_elevation.xodr"

# Inject target1=15 over 2s, wait 10 steps (0.5s, mid-transition), then inject
# target2=5 over 2s. The final speed must be target2 (5), never target1 (15).
run_driver d_inject inject_cruise.xosc 15 2 10 5 2
need_status 0 $? "d_inject"

need_close speed_before 10.0 0.01 "the scenario's own initial cruise speed"
# 0.5 s into a 2 s linear ramp from 10 to 15 the semantic value is 11.25; a
# build that starts applying the action one step later reads 11.125. Either
# is "heading toward the first target without having reached it", which is
# all R3 promises here, so the exact decimal is not pinned. The window
# [10.5, 12.5] excludes 10.0 (first injection never took effect) and 15.0
# (already settled, nothing left to override); the definitive obligation is
# speed_final below.
need_close speed_mid 11.5 1.0 "mid-transition toward the first injected target, before the override"
need_kv ongoing_at_second_inject 1 "the second injection reports ongoing immediately"
need_kv ongoing_after 0 "the injected action reports finished once it has settled"
need_close speed_final 5.0 0.01 "the object settles on the SECOND target, not the first"
need_kv survived 1 "driver completed"
exit 0
