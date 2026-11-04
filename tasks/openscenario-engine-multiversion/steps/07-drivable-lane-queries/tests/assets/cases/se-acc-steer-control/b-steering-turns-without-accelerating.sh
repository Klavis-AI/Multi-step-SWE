#!/usr/bin/env bash
# The two commanded quantities must be independent: steering with zero commanded
# acceleration must turn the vehicle while leaving its speed untouched. A model
# that couples them, or that ignores the steering argument, fails here.
source /tests/caselib.sh

# a = 0, steering = 0.2 rad, 20 steps of 0.1 s, starting from 10 m/s.
run_driver se_acc_steer 0.0 0.2 20 0.1
need_status 0 $? "se_acc_steer"
need_kv create 1 "simple vehicle created"

# Speed must be untouched at every sample, not merely at the end.
need_close speed_0  10.0 0.001 "initial speed"
need_close speed_5  10.0 0.001 "speed after 5 steps of pure steering"
need_close speed_10 10.0 0.001 "speed after 10 steps of pure steering"
need_close speed_20 10.0 0.001 "speed after 20 steps of pure steering"
need_close speed_final 10.0 0.001 "final speed under pure steering"

# The heading must actually have turned, and the path must have curved away from
# the axis. Starting heading is 0, so both are unambiguous.
#
# h_final is NOT derivable from requirement 11, which promises only that the
# heading turns and that the speed is left alone. The exact figure depends on
# which bicycle model the vehicle uses, and the inherited model is a specific
# one: the reference point sits 15% along the wheelbase, so the heading rate is
# speed*sin(atan(0.15*tan(d)))/(0.15*length). Held for 2 s at d = 0.2 rad, 10
# m/s and length 4 that gives 1.013082, which is what upstream prints. Other
# standard models land nearby but not on it - a rear-axle bicycle model gives
# 1.013550, a centre-of-gravity one 1.008384 - so the tolerance is set to hold
# any of them. It is still two orders of magnitude below the 1.013 rad that
# separates a vehicle that turns from one that holds its heading, which is what
# this assertion is here to catch.
#
# x_final and y_final are the integral of that heading, and requirement 11
# fixes no integration scheme: it promises the direction of the turn and the
# independence of the two commands, not the arithmetic that accumulates the
# path. Measured against the same steering input, a forward-Euler update steps
# out to 16.70 / 9.38, a midpoint update to 16.46 / 9.80, and a ten-times
# finer step to 16.48 / 9.76 - a spread of about 0.3 in x and 0.4 in y on a
# 20 m path. The tolerance is therefore set to cover the scheme, not the
# scheme's least significant digit: it is still far tighter than the 3.3 m in
# x and 9.4 m in y that separate a vehicle that turns from one that holds its
# heading, which is what this pair is here to catch.
need_close h_0 0.0 0.0001 "initial heading"
need_close h_final 1.01308 0.01 "heading after 2 s of steering"
need_close x_final 16.7047 0.6 "longitudinal position after the turn"
need_close y_final 9.3793  0.6 "lateral position after the turn"
need_kv survived 1 "driver completed"
exit 0
