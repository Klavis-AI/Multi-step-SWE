#!/usr/bin/env bash
# SE_ReportObjectWheelStatus and SE_GetPauseFlag, both new at this release.
#
# Three distinct answers are required from the same entry point -- before a
# scenario is loaded, for a live object, and for an object id that does not
# exist -- so a stub that returns one constant cannot pass this case whichever
# constant it picks.
source /tests/caselib.sh

run_driver se_pause_wheel "$FIXTURES/two_car_brake.xosc" || fail "se_pause_wheel exited $?"
need_kv wheel_before -1 "SE_ReportObjectWheelStatus before SE_Init"
need_kv init 0 "SE_Init"
need_kv wheel_valid 0 "SE_ReportObjectWheelStatus for a live object"
need_kv wheel_invalid -1 "SE_ReportObjectWheelStatus for an unknown object id"

# SE_GetPauseFlag likewise answers differently before and after a scenario is
# loaded. The --pause option does not exist at this release, so a stepped
# scenario is never paused and 0 is the only reachable loaded value.
need_kv pause_before -1 "SE_GetPauseFlag before SE_Init"
need_kv pause_after 0 "SE_GetPauseFlag on a stepped scenario"
need_kv survived 1 "driver ran to the end"
