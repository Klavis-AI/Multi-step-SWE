#!/usr/bin/env bash
# A single named element (the fixture's KeepSpeedManeuver) must be reported
# running before it is reported complete, and the callback must report the
# same element-type code both times - not a mix of a real value and a default.
source /tests/caselib.sh

run_driver sb_callback "$FIXTURES/param_cars.xosc" || fail "sb_callback exited $?"
need_kv keep_speed_states "2,3" "KeepSpeedManeuver must be seen running, then complete, in that order, exactly once each"
need_kv keep_speed_type 4 "the element-type code for a Maneuver is stable across both calls"
need_kv survived 1 "the engine keeps running after the callback is registered"
