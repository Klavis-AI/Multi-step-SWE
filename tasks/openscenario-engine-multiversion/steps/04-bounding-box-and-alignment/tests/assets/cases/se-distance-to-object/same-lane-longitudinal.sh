#!/usr/bin/env bash
# SE_GetDistanceToObject between two entities in the SAME lane, same
# direction: the reference-point measurement is the plain difference in their
# s coordinates, and the bounding-box ("free space") measurement subtracts
# both vehicles' half-lengths from it. Reversing which object asks about
# which flips the sign of the longitudinal delta.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_distance "$WORK/two_car_brake.xosc" 0 1
need_status 0 $? "se_distance on two_car_brake.xosc"

need_kv init 0        "SE_Init"
need_kv nobj 2         "the scenario declares two entities"
need_kv a_lane -1       "Ego's lane"
need_kv b_lane -1        "Target's lane, same as Ego"

need_kv ref_rc 0                "reference-point measurement succeeded"
need_close ref_ds 99.5 0.01       "reference-point longitudinal delta"
need_close ref_dt 0.0 0.01          "no lateral offset between same-lane entities"
need_kv ref_dlane 0                  "no lane delta between same-lane entities"
need_kv ref_opposite 0                "same direction of travel"

# dx / dy are the same A -> B offset as ds / dt, expressed in the global frame
# instead of the road frame. On this fixture the objects share one lane and one
# heading (both lane -1, h = 0), so the two frames coincide: dy is 0 and dx
# equals ds, which the state dump above confirms (a_x 21.0, b_x 120.5,
# difference 99.5).
need_close ref_dx 99.5 0.01             "global-frame x displacement, A -> B"
need_close ref_dy 0.0 0.01               "global-frame y displacement, no lateral component in a single lane at h = 0"

# The reference-point rows above are the contract dx/dy are FOR: with
# free_space false the four values are one displacement in two frames, and
# dx/dy are always the global form of the ds/dt beside them.
#
# With free_space TRUE they are not, and instruction.md section 4 states the
# mechanism this case measures: the box path scans all sixteen corner pairs,
# EVERY pair assigns ds/dt/dx/dy, and when the scan ends ds/dt are replaced by
# the scan minima while dx/dy keep the LAST pair's values -- corner 3 of each
# box, at (center.x + length/2, center.y - width/2) in the object's own frame,
# rotated by its heading and translated to its position.
#
# Here both vehicles carry the identical box (centre 1.4,0; 5.0 x 2.0) at
# heading 0 in the same lane, so corner 3 sits at the same offset from both
# reference points and the offset cancels in the difference: free_dx is
# exactly b_x - a_x = 99.5, the same value the reference-point query returns,
# and free_dy is 0.0. Only free_ds moves to the near-edge 94.5. The a_x / b_x
# rows in the driver's output are the cross-check, so both numbers below are
# derived from the fixture rather than merely observed.
#
# 94.5 is NOT what free_dx returns here: only ds/dt are recomputed onto the
# box edges. The instruction says so in as many words.
need_kv free_rc 0                       "bounding-box measurement succeeded"
need_close free_ds 94.5 0.01              "bounding-box delta is shorter by both half-lengths"
need_close free_dx 99.5 0.01                "free-space dx is the box scan's last corner pair, which on two identical boxes is b_x - a_x"
need_close free_dy 0.0 0.01                  "still no lateral component between two same-lane boxes"

need_kv rev_rc 0                            "the reversed query succeeded"
need_close rev_ds -99.5 0.01                  "reversing the pair flips the sign of the longitudinal delta"
need_close rev_dx -99.5 0.01                    "and flips the global-frame x displacement with it"

need_neg unknown_rc    "a distance query against an unknown object id is rejected"
need_kv survived 1      "driver ran to the end"

# ---------------------------------------------------------------------------
# The refusal half: two objects with no route between them.
# ---------------------------------------------------------------------------
# Everything above measures a distance that exists. instruction.md section 4
# also states that the call REFUSES -- returns negative -- when no path between
# the two positions can be found, and until now the only refusal this group
# exercised was the unknown-id one on line 63, which a stub can satisfy with a
# single bounds check on the id.
#
# islands.xosc places its two entities on two roads of two_islands.xodr that
# carry no link elements between them at all: road 1 and road 2 are disjoint
# components of the network, 2000 m apart in y. There is no route from one to
# the other, so the reference-point query has nothing to measure.
#
# Only the reference-point query is graded here, and section 4 says why: the
# free-space path does not route at all. It scans the sixteen bounding-box
# corner pairs and reports the straight-line geometry between them, so it
# answers 0 with a real dy across the gap even though no road connects the two.
# Asserting a refusal from the free-space query would assert something the
# implementation does not do.
cp "$FIXTURES/islands.xosc" "$FIXTURES/two_islands.xodr" "$WORK/"

run_driver se_distance "$WORK/islands.xosc" 0 1
need_status 0 $? "se_distance on islands.xosc"
need_kv init 0 "SE_Init (disjoint network)"
need_kv nobj 2 "islands.xosc declares two entities"

# Both entities loaded and are where the scenario put them -- so the refusal
# below is a routing refusal and not a failure to place the objects.
need_kv a_lane -1 "Ego is in lane -1 of road 1"
need_kv b_lane -1 "Island is in lane -1 of road 2"
need_close a_s 50.0 0.01 "Ego sits at s = 50 on its road"
need_close b_s 50.0 0.01 "Island sits at s = 50 on its road"

# The sign only. The contract states the refusal is negative and does not fix
# which negative value it is, so pinning one would pin something unstated.
need_neg ref_rc "no route exists between the two roads, so the reference-point query refuses"
need_neg rev_rc "and it refuses the same way with the pair reversed"

need_neg unknown_rc "an unknown object id is still rejected on this network too"
# instruction.md section 4: the unknown-id rejection is "required of both"
# queries, and the free-space query is the one a reader could forget.
need_neg unknown_free_rc "an unknown object id is rejected by the free-space query too"

# And the other half of that paragraph: the free-space query does not route, so
# a disconnected pair is not a no-path case for it -- it returns 0 and hands
# back a real corner-pair displacement across the gap. The reference-point
# refusals above are the discriminating pair.
need_kv free_rc 0 "the free-space query succeeds across a disconnected pair"
need_kv survived 1 "driver ran to the end (disjoint network)"
