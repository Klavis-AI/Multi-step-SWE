#!/usr/bin/env bash
# NEW. SE_ScenarioObjectState's pose fields are double, not float, and a pose
# reported through SE_ReportObjectPosXYH must come back out of
# SE_GetObjectState without being rounded to 32-bit precision.
#
# The driver carries a static assertion on the declared member type, so it does
# not compile at all before the migration. The runtime half below is the part
# that a solution which merely re-typedefs the struct without widening the
# pipeline would still fail: the tolerances are 1e-12, and a value that passes
# through a single float anywhere loses about 5e-8 on these inputs - roughly
# five orders of magnitude too much.
source /tests/caselib.sh

run_driver se_state "$FIXTURES/two_car_brake.xosc" 12.3456789 4.5678901 0.7853981633974483 0.05
need_status 0 $? "se_state"

need_kv init 0     "SE_Init"
need_kv nobj 2     "two entities in the scenario"
need_kv xyh_rc 0   "SE_ReportObjectPosXYH accepted the pose"
need_kv step_rc 0  "SE_StepDT"
need_kv get_rc 0   "SE_GetObjectState"

need_close x 12.3456789          1e-12 "reported x survives the round trip in double precision"
need_close y 4.5678901           1e-12 "reported y survives the round trip in double precision"
need_close h 0.7853981633974483  1e-12 "reported heading survives the round trip in double precision"

# The clock is double too: 0.05 is not representable in binary, and a float
# timestamp lands ~1e-9 away from it.
need_close timestamp 0.05 1e-15 "timestamp is a double"

# Bounding-box dimensions come from the scenario, not from the reported pose;
# they are asserted to show the whole struct was read, not just its head. The
# instruction names length, width and height together, so all three are read
# back; the fixture declares 5.0 x 2.0 x 1.5. The driver also carries a
# static assertion on each of the three declared member types, so a header that
# widened only `length` does not compile.
need_close length 5   1e-12 "entity length"
need_close width  2   1e-12 "entity width"
need_close height 1.5 1e-12 "entity height"

need_kv survived 1 "driver reached SE_Close without aborting"
