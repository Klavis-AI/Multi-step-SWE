#!/usr/bin/env bash
# NEW. The motion-state reporters and getters move to double: velocities,
# angular rates and accelerations are all reported and read back as doubles,
# and the getters take double out-parameters.
source /tests/caselib.sh

run_driver se_motion "$FIXTURES/two_car_brake.xosc" 13.3333333 0.0 0.25 10 0.1
need_status 0 $? "se_motion"

# Before SE_Init there is no object to ask about. A negative return is the
# documented answer; asserting it pins that the getters fail cleanly rather
# than writing through an unset out-parameter.
need_kv vel_before -1  "SE_GetObjectVelocityGlobalXYZ before init"
need_kv rate_before -1 "SE_GetObjectAngularVelocity before init"

need_kv init 0 "SE_Init"

need_kv repvel_rc 0      "SE_ReportObjectVel on a real object"
need_kv repvel_rc_bad -1 "SE_ReportObjectVel on an object id that does not exist"
need_kv repang_rc 0      "SE_ReportObjectAngularVel on a real object"
need_kv repang_rc_bad -1 "SE_ReportObjectAngularVel on an object id that does not exist"

need_kv vel_rc 0 "SE_GetObjectVelocityGlobalXYZ"
# 13.3333333 is not representable in binary; a float round trip lands about
# 4e-8 away, a double keeps it to within 1e-12.
need_close vel_x 13.3333333 1e-12 "reported global x velocity survives in double precision"
need_close vel_y 0          1e-15 "no lateral velocity was reported"

need_kv rate_rc 0 "SE_GetObjectAngularVelocity"
need_close rate_h 0.25 1e-15 "reported heading rate reads back exactly"

need_kv acclat_rc 0 "SE_GetObjectAccelerationLocalLatLong"
need_close acc_lat 0 1e-15 "no lateral acceleration on a straight-line report"

need_kv survived 1 "driver reached SE_Close without aborting"
