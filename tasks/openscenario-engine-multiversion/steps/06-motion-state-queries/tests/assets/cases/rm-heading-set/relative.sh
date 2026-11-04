#!/usr/bin/env bash
# The relative form of RM_SetHMode sets the angle between the entity and the
# road. On an arc the road has a heading of its own, so a relative request and
# an absolute one for the same number produce different world headings -- which
# is the only place the two modes can be told apart.
#
# At s=600 on curve_r100 the road heading is 1.0 rad.
source /tests/caselib.sh

# Relative (RM_H_REL): the argument becomes the heading relative to the road.
run_driver rm_heading "$FIXTURES/curve_r100.xodr" 0 -1 600.0 0.5 112 || fail "rm_heading exited $?"
need_kv survived 1 "driver ran to completion"
need_kv used_mode_form 1 "the RM_SetHMode form was used"
need_kv set_h_ok 1 "RM_SetHMode returned >= 0"
need_close before_h 1.0 0.001 "road heading at s=600 before the change"
need_close before_hrel 0.0 0.001 "relative heading before the change"
need_close after_hrel 0.5 0.0001 "relative heading set to the argument"
need_close after_h 1.5 0.0001 "world heading is the road heading plus the argument"

# Absolute (RM_H_ABS) on the same position: the argument IS the world heading.
run_driver rm_heading "$FIXTURES/curve_r100.xodr" 0 -1 600.0 0.5 48 || fail "rm_heading exited $?"
need_kv set_h_ok 1 "RM_SetHMode returned >= 0"
need_close after_h 0.5 0.0001 "absolute mode sets the world heading"
need_close after_hrel 5.78319 0.0001 "relative heading follows from the world heading"

# Neither mode moved the position.
need_close after_x 585.4388 0.001 "x unchanged"
need_close after_y 45.1404 0.001 "y unchanged"
need_close after_s 600.0 0.001 "s unchanged"
