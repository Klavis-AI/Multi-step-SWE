#!/usr/bin/env bash
# RM_GetNumberOfRoadsOverlapping / RM_GetOverlappingRoadId inside a junction.
# fabriksgatan.xodr has junction 4 built from connecting roads 5..16; the point
# (25, 0) sits where four of them cross.
source /tests/caselib.sh

run_driver rm_overlap "$FIXTURES/fabriksgatan.xodr" 25 0 || fail "rm_overlap exited $?"
need_kv init 0 "RM_Init"
need_kv road_id 7 "road under (25,0)"
need_kv junction_id 4 "junction under (25,0)"
need_kv n_overlap 4 "RM_GetNumberOfRoadsOverlapping at (25,0)"

# The contract does not fix an iteration order for RM_GetOverlappingRoadId, so
# this compares the SET of road ids returned across index 0..3, not the order
# they came out in.
got=$(sed -n 's/^overlap_[0-9]*=//p' "$WORK/last.out" | sort -n | tr '\n' ' ')
want="7 13 14 15 "
[ "$got" = "$want" ] || fail "overlapping road ids: expected the set {7,13,14,15}, got {$got}"

need_kv survived 1 "driver ran to the end"
