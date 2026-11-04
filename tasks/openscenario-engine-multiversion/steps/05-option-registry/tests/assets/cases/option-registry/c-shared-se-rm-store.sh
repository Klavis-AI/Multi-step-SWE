#!/usr/bin/env bash
# New. The scenario engine and the road manager are two entry points onto one
# process-global option store: an option set through one side must be visible
# through the other.
source /tests/caselib.sh

cp "$FIXTURES/elev_cruise.xosc" "$WORK/elev_cruise.xosc"
cp "$FIXTURES/curves_elevation.xodr" "$WORK/curves_elevation.xodr"

run_driver d_options elev_cruise.xosc
need_status 0 $? "d_options"

need_kv shared_se_sees 1 "SE_GetOptionSet sees an option RM_SetOption stored"
need_kv shared_rm_sees 1 "RM_GetOptionSet sees an option SE_SetOption stored"
need_kv survived 1 "driver completed"
exit 0
