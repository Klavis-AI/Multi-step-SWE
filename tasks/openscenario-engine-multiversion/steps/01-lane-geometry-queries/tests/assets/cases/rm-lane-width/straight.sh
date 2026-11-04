#!/usr/bin/env bash
# RM_GetLaneWidth / RM_GetLaneWidthByRoadId on a single-lane-section road.
# straight_500m.xodr has one road (id 1) with one lane section, so the width is
# constant in s and the two entry points must agree exactly.
source /tests/caselib.sh

run_driver rm_lane "$FIXTURES/straight_500m.xodr" 1 -1 250 || fail "rm_lane exited $?"
need_kv init 0 "RM_Init"
need_kv nroads 1 "road count"
need_close width_by_road 3.07 0.001 "RM_GetLaneWidthByRoadId(1,-1,250)"
need_close width_handle 3.07 0.001 "RM_GetLaneWidth(handle,-1)"

# The same query at a different s on the same lane section returns the same
# width: a stub that echoes its own s argument does not survive this.
run_driver rm_lane "$FIXTURES/straight_500m.xodr" 1 -1 10 || fail "rm_lane exited $?"
need_close width_by_road 3.07 0.001 "width at s=10"

# The centre lane has no width at all, and 0 is a real answer here rather than
# the absence of one.
run_driver rm_lane "$FIXTURES/straight_500m.xodr" 1 0 250 || fail "rm_lane exited $?"
need_close width_by_road 0.0 0.001 "centre lane width"

# --- widths that are graded along s -----------------------------------------
# Everything above holds on a road whose <width> entries are constant
# (a=..., b=c=d=0), which an implementation that returned only the `a`
# coefficient would also satisfy. graded_width_200m.xodr is the counterexample
# instruction.md section 1 describes: two lane sections, and within each one a
# real cubic in the distance from THAT SECTION's start.
#
#   section 0 (road s = 0..100), ds = s
#       lane -1   w = 3.0 + 1.0e-2 * ds
#       lane -2   w = 2.0 + 1.0e-4 * ds^2
#   section 1 (road s = 100..200), ds = s - 100
#       lane -1   w = 5.0 - 2.0e-2 * ds
#       lane -2   w = 3.0 + 1.0e-6 * ds^3
#
# Read straight off the fixture's <width> coefficients, not off a run: each
# expectation below is the polynomial evaluated by hand at that s.
graded() {  # graded <lane> <s> <expected>
    run_driver rm_lane "$FIXTURES/graded_width_200m.xodr" 1 "$1" "$2" \
        || fail "rm_lane on graded_width_200m lane $1 s=$2 exited $?"
    need_kv init 0 "graded fixture loads"
    need_close width_by_road "$3" 0.001 "RM_GetLaneWidthByRoadId(1,$1,$2)"
    need_close width_handle  "$3" 0.001 "RM_GetLaneWidth(handle,$1) at s=$2"
}

# Linear term: three different answers on one lane of one lane section. A
# build that dropped b, c and d would report 3.0 at all three.
graded -1   0 3.000
graded -1  50 3.500
graded -1  99 3.990

# Quadratic and cubic terms, and the section restart: at road s=150 the second
# section's ds is 50, not 150, so lane -1 is 5.0 - 2.0e-2*50 = 4.0. Measuring
# ds from the road origin would give 2.0 here and is the other way this is
# commonly got wrong.
graded -2   0 2.000
graded -2  50 2.250
graded -2  99 2.9801
graded -1 150 4.000
graded -1 199 3.020
graded -2 150 3.125
graded -2 199 3.970299

# A constant entry in the same file still behaves like one, so the grading
# above is read from the coefficients rather than applied to every lane.
graded  1  50 3.500
graded  1 150 3.500

# --- widths with nothing to answer about ------------------------------------
# instruction.md section 1: there is no error return and no reserved code. A
# lane id the road does not have, a road id the network does not contain, and
# a handle RM_CreatePosition returned but no setter ever placed all answer
# 0.0 -- the same answer the centre lane gives above.
#
# rm_lane_absent asks all three through one process and, in the same run, asks
# the real lane as well: a build that answered 0.0 to everything satisfies the
# first three and fails on present_width.
run_driver rm_lane_absent "$FIXTURES/straight_500m.xodr" 1 -1 250 -99 999 \
    || fail "rm_lane_absent exited $?"
need_kv init 0 "RM_Init"
need_kv nroads 1 "road count"
need_kv pos_lane -1 "the probe handle is parked on a real lane"
need_close unplaced_width          0.0 0.001 "width from a handle no setter has placed"
need_close absent_lane_width_handle 0.0 0.001 "RM_GetLaneWidth on a lane id the road does not have"
need_close absent_lane_width_by_road 0.0 0.001 "RM_GetLaneWidthByRoadId on a lane id the road does not have"
need_close absent_road_width       0.0 0.001 "RM_GetLaneWidthByRoadId on a road id the network does not contain"
need_close present_width           3.07 0.001 "the real lane still answers in the same run"
need_kv survived 1 "none of the above brought the process down"
