#!/usr/bin/env bash
# RM_SetH and the absolute form of RM_SetHMode set the heading of an existing
# position. The position's world coordinates, station, lane and road must all
# come back unchanged: only the heading moves.
#
# Measured on a road that is straight, so on a road whose own heading is zero
# the two forms are indistinguishable in the result.
source /tests/caselib.sh

run_driver rm_heading "$FIXTURES/straight_500m.xodr" 1 -1 50.0 1.5708 0 || fail "rm_heading exited $?"
need_kv survived 1 "driver ran to completion"
need_kv init 0 "RM_Init"
need_kv set_lane_ok 1 "RM_SetLanePosition returned >= 0"
need_kv used_mode_form 0 "the plain RM_SetH form was used"
need_kv set_h_ok 1 "RM_SetH returned >= 0"
need_close after_h 1.5708 0.0001 "heading set to pi/2"

# Everything but the heading is untouched.
need_close after_x 50.0 0.0001 "x unchanged"
need_close after_y -1.535 0.0001 "y unchanged"
need_close after_s 50.0 0.0001 "s unchanged"
need_kv after_lane -1 "lane unchanged"
need_kv after_road 1 "road unchanged"

# The explicit-mode absolute form agrees with the plain form.
run_driver rm_heading "$FIXTURES/straight_500m.xodr" 1 -1 50.0 1.5708 48 || fail "rm_heading exited $?"
need_kv used_mode_form 1 "the RM_SetHMode form was used"
need_kv set_h_ok 1 "RM_SetHMode returned >= 0"
need_close after_h 1.5708 0.0001 "absolute mode sets the heading to the given value"
need_close after_x 50.0 0.0001 "x unchanged in absolute mode"
need_close after_s 50.0 0.0001 "s unchanged in absolute mode"

need_neg rc_bad_handle "an unknown handle reports failure"
need_neg rc_bad_handle_mode "an unknown handle reports failure in the explicit-mode form"
