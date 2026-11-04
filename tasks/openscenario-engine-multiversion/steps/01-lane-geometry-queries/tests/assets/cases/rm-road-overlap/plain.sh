#!/usr/bin/env bash
# The same two queries away from a junction, and an out-of-range index.
source /tests/caselib.sh

# A point on a road that crosses nothing: the road overlaps only itself.
run_driver rm_overlap "$FIXTURES/straight_500m.xodr" 250 0 || fail "rm_overlap exited $?"
need_kv road_id 1 "road under (250,0)"
need_kv junction_id -1 "not in a junction"
need_kv n_overlap 1 "overlap count on a plain road"
need_kv overlap_0 1 "the road itself"
# An index past the end is an error return, not a crash and not a stale id.
need_kv overlap_oob -1 "RM_GetOverlappingRoadId past the last index"

# A point off the junction inside fabriksgatan: still no overlap, and this
# proves the count above is read from geometry rather than from the file name.
run_driver rm_overlap "$FIXTURES/fabriksgatan.xodr" 0 0 || fail "rm_overlap exited $?"
need_kv road_id 3 "road under (0,0)"
need_kv junction_id -1 "not in a junction"
need_kv n_overlap 0 "overlap count away from the junction"
need_kv survived 1 "driver ran to the end"
