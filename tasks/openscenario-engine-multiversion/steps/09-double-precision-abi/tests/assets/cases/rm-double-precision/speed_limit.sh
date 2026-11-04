#!/usr/bin/env bash
# RM_GetSpeedLimit and RM_GetRoadLength return double. The static assertions on
# the declared return types fail at v2.59.0; the two runtime values below are
# what separates a genuinely widened implementation from one that widened the
# declarations and left single precision in the pipeline behind them.
#
# Both quantities are chosen because they are not binary fractions. 60 km/h is
# 16.666666666666668 m/s, which a float round trip moves by 6.4e-7 - six orders
# of magnitude outside the 1e-12 asserted here. Road 0 of fabriksgatan is
# 93.660831225697507 m long, which a float moves by 3.6e-6, three and a half
# orders outside the 1e-9 asserted here. Neither number is a round figure: both
# are the double-precision values this road network actually carries.
source /tests/caselib.sh

run_driver rm_double "$FIXTURES/fabriksgatan.xodr" 0 -1 50.0 100.0 -1.5
need_status 0 $? "rm_double"
need_kv init 0 "RM_Init"
need_kv nroads 16 "road count"
need_kv survived 1 "RM_Close"

# Road length reported as double.
need_close road_length 93.660831225697507 1e-9 "RM_GetRoadLength"

# Speed limit reported as double: 60 km/h in m/s.
need_close speed_limit 16.666666666666668 1e-12 "RM_GetSpeedLimit"

# Position s round-trip. The world point (100, -1.5) resolves onto a road of
# this network; which one is geometry, so only the range is asserted here -
# the precision of the s round trip is rm-probe-abi's assertion.
s_out=$(sed -n 's/^pos_s=//p' "$WORK/last.out")
[ -n "$s_out" ] || fail "driver printed no pos_s= line"
awk -v s="$s_out" 'BEGIN { exit (s > 0 && s < 200) ? 0 : 1 }' \
    || fail "position s out of expected range, got $s_out"
