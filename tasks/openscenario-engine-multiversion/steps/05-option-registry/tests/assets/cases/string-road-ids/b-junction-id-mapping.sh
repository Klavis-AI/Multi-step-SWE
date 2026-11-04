#!/usr/bin/env bash
# New. Junctions carry string ids through the same mapping surface as roads,
# and an unknown junction id string must report the same not-found sentinel.
source /tests/caselib.sh

cp "$FIXTURES/string_ids.xodr" "$WORK/string_ids.xodr"
cp "$FIXTURES/elev_cruise.xosc" "$WORK/elev_cruise.xosc"
cp "$FIXTURES/curves_elevation.xodr" "$WORK/curves_elevation.xodr"

run_driver d_string_ids string_ids.xodr elev_cruise.xosc
need_status 0 $? "d_string_ids"

need_kv jx 0 "JX junction internal id"
need_kv jxstr JX "reverse mapping of JX's internal id"
need_kv jxmissing 4294967295 "unknown junction id string returns the not-found sentinel"

# R05.1's junction half. SE_GetRoadInfoAtDistance reports junctionId for a
# lookahead target, and the road this scenario runs on meets no junction, so
# every sample must report SE_ID_UNDEFINED -- never 0, which is a real
# junction's id in this same fixture (see `jx` above) and therefore the one
# value a build that forgot to set the field would print.
need_kv roadinfo_lookups_ge1 1 "SE_GetRoadInfoAtDistance answered at least one lookahead sample"
need_kv roadinfo_defined_junctions 0 "a junction-free road reports the undefined junction id at every sample"

need_kv survived 1 "driver completed"
exit 0
