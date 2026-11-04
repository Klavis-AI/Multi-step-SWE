#!/usr/bin/env bash
# SE_GetDistanceToObject between two stationary entities in OPPOSITE driving
# lanes of the same road, with different bounding-box dimensions: this
# exercises the lateral (dt), lane-delta and opposite-direction outputs that
# the same-lane case cannot, and the bounding-box measurement subtracts each
# vehicle's own half-width as well as half-length.
#
# Reversing the pair here does NOT flip the sign of the longitudinal delta
# the way the same-lane case does: the second object faces the opposite
# driving direction, so "ahead" is measured against ITS OWN heading, and
# that reference-frame flip is exactly what oppositeLanes=1 signals. Measured
# from the real binary; not something to "simplify" back to a sign flip.
source /tests/caselib.sh

cp "$FIXTURES/lane_pair.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_distance "$WORK/lane_pair.xosc" 0 1
need_status 0 $? "se_distance on lane_pair.xosc"

need_kv init 0        "SE_Init"
need_kv a_lane -1       "Ego's lane"
need_kv b_lane 1         "Oncoming's lane, on the other side of the centreline"

need_kv ref_rc 0                 "reference-point measurement succeeded"
need_close ref_ds 150.0 0.01       "reference-point longitudinal delta"
need_close ref_dt 3.07 0.01          "reference-point lateral delta"
need_kv ref_dlane 1                    "one lane separates the two reference points"
need_kv ref_opposite 1                   "the two lanes carry opposite-direction traffic"

need_kv free_rc 0                          "bounding-box measurement succeeded"
need_close free_ds 138.1 0.01                "bounding-box delta is shorter by both half-lengths"
need_close free_dt 0.82 0.01                   "bounding-box lateral delta is narrower by both half-widths"

# dx / dy repeat the same A -> B offset in the global frame. Ego sits at
# x = 50 heading 0, Oncoming at x = 200 heading pi, so the reference-point A->B
# vector is (150, 3.07) -- its road-frame pair (ds 150, dt 3.07) and its
# global-frame pair agree here for the longitudinal component and differ for
# the lateral one only through the frame, not the magnitude.
need_close ref_dx 150.0 0.01                     "global-frame x displacement, A -> B"
need_close ref_dy 3.07 0.01                        "global-frame y displacement across the centreline"

# As in the same-lane case, the free-space dx/dy are the LAST corner pair of
# the box scan, not the near-edge offset resolved in global axes.
# instruction.md section 4 states the mechanism and this arithmetic: corner 3
# of a box is (center.x + length/2, center.y - width/2) in the object's own
# frame, rotated by its heading and translated to its position.
#
#   Ego      x 50,  lane -1 (y -1.535), h 0 , centre 1.4,0, 5.0 x 2.0
#              -> corner 3 = (50.0 + 3.9, -1.535 - 1.00)  = ( 53.9, -2.535)
#   Oncoming x 200, lane  1 (y  1.535), h pi, centre 3.5,0, 9.0 x 2.5
#              -> corner 3 = (200.0 - 8.0,  1.535 + 1.25) = (192.0,  2.785)
#   difference                                            = (138.1,  5.320)
#
# That is why free_dy (5.32) comes back WIDER than the reference-point lateral
# gap (3.07), while free_dt -- the field that carries the real box-edge
# measurement on this path -- correctly comes back narrower, at 0.82. Both
# values below are derived from the fixture, not merely observed.
need_close free_dx 138.1 0.01                    "free-space dx is the box scan's last corner pair, not a recomputed edge offset"
need_close free_dy 5.32 0.01                      "free-space dy is that same corner pair, so it is wider than the reference-point lateral gap"

need_kv rev_rc 0                                 "the reversed query succeeded"
need_close rev_ds 150.0 0.01                       "from Oncoming's own heading, Ego is still ahead"
need_close rev_dt -3.07 0.01                         "the lateral delta's sign flips with the query direction"
need_kv rev_dlane -1                                   "the lane delta's sign flips with the query direction"

# dx/dy are resolved in the axes of whichever object is ASKING, so reversing
# the query negates the displacement and re-resolves it into the other
# object's axes. Here the two objects face opposite ways, so both negations
# apply and cancel: dx/dy come back IDENTICAL either way, unlike dt and
# dLaneId above, and unlike the same-lane pair where the two objects share a
# heading and the reversal is a plain sign flip. instruction.md section 4
# states this; these two rows are what make it graded rather than merely
# claimed.
need_close rev_dx 150.0 0.01                             "reversing an opposite-facing pair leaves the global x displacement unchanged"
need_close rev_dy 3.07 0.01                                "and leaves the global y displacement unchanged with it"

need_kv survived 1  "driver ran to the end"
