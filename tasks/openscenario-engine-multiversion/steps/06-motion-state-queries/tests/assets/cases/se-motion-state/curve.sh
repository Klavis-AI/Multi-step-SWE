#!/usr/bin/env bash
# The same queries in steady cornering. The entity holds 20 m/s around the
# arc in curve_r100.xodr, whose REFERENCE LINE has radius 100 m. The entity
# drives lane -1, whose centre sits at t = -1.535, and the arc bends left, so
# the lane is on the OUTSIDE of it and the circle the entity actually travels
# has radius 100 + 1.535 = 101.535 m. The heading rate therefore settles at
# v/r = 20/101.535 = 0.19698 rad/s, not 20/100 = 0.2 - which is why the
# handoff states the lane offset for this fixture as well as the straight.
# The velocity vector turns with it while its magnitude stays 20.
#
# The heading ACCELERATION peak is deliberately not asserted on: entering the
# arc is a step in curvature, so the one-step difference that the library
# reports there is 0.2/dt and depends on the timestep rather than on physics.
# Only the settled rate is graded.
source /tests/caselib.sh

run_driver se_motion "$FIXTURES/cruise_curve.xosc" 800 0.01 || fail "se_motion exited $?"
need_kv survived 1 "driver ran to completion"
need_kv odo_monotonic 1 "odometer never decreases"

# 20 m/s for 8 s.
need_close odo 160.0 0.05 "odometer = 20 m/s * 8 s"
need_close sim_time 8.0 0.001 "simulation time"

# Steady-state heading rate is speed / radius of the travelled circle.
need_kv rc_angvel 0 "SE_GetObjectAngularVelocity succeeded"
need_close h_rate 0.19698 0.001 "heading rate = v/r = 20/101.535"
need_close p_rate 0.0 0.0001 "pitch rate stays zero"
need_close r_rate 0.0 0.0001 "roll rate stays zero"

# The velocity vector turns with the road but keeps its magnitude: on a curve
# both components are non-zero, which a constant-returning stub cannot fake.
#
# The COMPONENT tolerance is deliberately loose, for the same reason the heading
# acceleration above is not asserted at all. Requirement 2 says the components
# must reflect the instantaneous velocity vector; it does not say where in the
# step the sample is taken, and two defensible readings disagree by half a step
# of turn. Sampling the pose heading at the end of the step gives
# 20*(cos h, sin h) = (9.3665, 17.6711); differencing the position over the step
# gives the mid-step chord tangent, (9.3839, 17.6619), which is the same
# direction rotated back by w*dt/2 = (20/101.535)*0.01/2 = 0.00098 rad. The two
# are 0.0174 apart in x, so a tolerance tight enough to pick one convention
# would fail a conforming implementation that chose the other. 0.05 admits both
# with margin and costs nothing that is actually being graded here: the
# magnitude is pinned to 0.01 just below, the travelled radius is pinned by
# h_rate to 0.001 above, and every wrong-frame or constant answer - local-frame
# (20,0,0), swapped components, a sign error - is off by 8 or more.
need_kv rc_vel 0 "SE_GetObjectVelocityGlobalXYZ succeeded"
need_close vel_norm 20.0 0.01 "velocity magnitude on the arc"
need_close vel_x 9.3839 0.05 "velocity x on the arc"
need_close vel_y 17.6619 0.05 "velocity y on the arc"
