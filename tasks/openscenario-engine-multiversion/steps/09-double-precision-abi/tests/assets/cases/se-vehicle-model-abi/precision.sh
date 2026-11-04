#!/usr/bin/env bash
# The simple vehicle model's entry points and its state struct are float at
# v2.59.0, so the static assertions in se_vehicle do not compile there. The
# runtime half is the same argument the rest of the step makes: a coordinate
# that needs more than 24 mantissa bits must survive being handed in and read
# back out. 123.456789012345 is not a binary fraction - a float round trip
# moves it by about 1.9e-6, six orders of magnitude outside the 1e-12 asserted
# here.
source /tests/caselib.sh

X=123.456789012345
Y=-1.53456789012345
H=0.25
SPD=17.1234567890123

run_driver se_vehicle "$X" "$Y" "$H" "$SPD"
need_status 0 $? "se_vehicle"
need_kv created 1 "SE_SimpleVehicleCreate"
need_kv survived 1 "SE_SimpleVehicleDelete"

need_close x0 "$X"   1e-12 "creation x read back through SE_SimpleVehicleState"
need_close y0 "$Y"   1e-12 "creation y read back through SE_SimpleVehicleState"
need_close h0 "$H"   1e-12 "creation heading read back through SE_SimpleVehicleState"
need_close speed0 "$SPD" 1e-12 "speed set through SE_SimpleVehicleSetSpeed"

# One second of coasting from a standstill-free start: the model integrates, so
# only the direction of travel is asserted. The vehicle is pointed at h=0.25 rad
# and is moving, so x must have advanced.
x1=$(sed -n 's/^x1=//p' "$WORK/last.out")
[ -n "$x1" ] || fail "driver printed no x1= line"
awk -v a="$x1" -v b="$X" 'BEGIN { exit (a > b) ? 0 : 1 }' \
    || fail "vehicle did not advance in x over one second: $x1 vs $X"
