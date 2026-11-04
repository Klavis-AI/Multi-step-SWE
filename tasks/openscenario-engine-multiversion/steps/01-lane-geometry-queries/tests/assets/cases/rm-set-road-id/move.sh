#!/usr/bin/env bash
# RM_SetRoadId re-anchors an existing position handle onto a named road,
# keeping the world coordinates and re-deriving the road-relative ones.
#
# (20, 0) in fabriksgatan.xodr sits inside junction 4, where the road manager's
# own closest-road answer is connecting road 7. Roads 2 and 3 are ordinary
# roads outside the junction, so asking for either is a request the position
# would never have reached on its own -- which is what makes it a test of the
# call rather than of the geometry.
source /tests/caselib.sh

run_driver rm_setroad "$FIXTURES/fabriksgatan.xodr" 20 0 2 || fail "rm_setroad exited $?"
need_kv init 0 "RM_Init"
need_kv road_before 7 "road the position resolves to on its own"
need_kv set_rc 0 "RM_SetRoadId return code"
need_kv road_after 2 "road after RM_SetRoadId(handle, 2)"

# The world position is what is kept; only the road-relative interpretation of
# it changes.
need_close x_before 20.0 0.001 "x before"
need_close x_after 20.0 0.001 "x is preserved"
need_close y_before 0.0 0.001 "y before"
need_close y_after 0.0 0.001 "y is preserved"

# A second road, so the answer is read from the argument rather than being one
# fixed fallback.
run_driver rm_setroad "$FIXTURES/fabriksgatan.xodr" 20 0 3 || fail "rm_setroad exited $?"
need_kv set_rc 0 "RM_SetRoadId return code"
need_kv road_after 3 "road after RM_SetRoadId(handle, 3)"
need_close x_after 20.0 0.001 "x is preserved"
