#!/usr/bin/env bash
# Regression floor, second half: the same query set against two other networks,
# and the failure path. Measured identical at both ends of this step.
#
# tri_lane_600m.xodr is a single straight road with wider lanes;
# fabriksgatan.xodr is a real sixteen-road junction network, so the road count,
# the id of the first road and the measured length are all different from the
# single-road case -- a stub that answers from a constant cannot satisfy both.
source /tests/caselib.sh

cp "$FIXTURES/tri_lane_600m.xodr" "$FIXTURES/fabriksgatan.xodr" "$WORK/"

run_driver rm_core "$WORK/tri_lane_600m.xodr" 1 -2 300
need_status 0 $? "rm_core on tri_lane_600m"
need_kv init 0 "RM_Init on tri_lane_600m"
need_kv nroads 1 "one road"
need_kv first_road_id 1 "road id"
need_kv road_length 600.000 "RM_GetRoadLength"
need_kv nlanes 7 "RM_GetRoadNumberOfLanes"
need_grep '^pos_road=1 pos_lane=-2 pos_s=300\.000$' "$WORK/last.out" "middle lane position"
need_grep '^width_rc=1 width=3\.5000$' "$WORK/last.out" "middle lane width"
need_kv survived 1 "RM_Close on tri_lane_600m"

run_driver rm_core "$WORK/fabriksgatan.xodr" 0 -1 10
need_status 0 $? "rm_core on fabriksgatan"
need_kv init 0 "RM_Init on fabriksgatan"
need_kv nroads 16 "the junction network's road count"
need_kv first_road_id 0 "id of the first road by index"
need_kv road_length 93.661 "RM_GetRoadLength on a real road"
need_grep '^pos_road=0 pos_lane=-1 pos_s=10\.000$' "$WORK/last.out" "position on the junction network"
need_grep '^width_rc=1 width=3\.5000$' "$WORK/last.out" "lane width on the junction network"
need_kv survived 1 "RM_Close on fabriksgatan"

# A network that is not there is a reported failure, not a crash and not a
# silent success. The driver stops after RM_Init fails, so it exits 1 and never
# prints its survived= line.
run_driver rm_core "$WORK/no_such_network.xodr" 1 -1 10
rc=$?
[ "$rc" -eq 1 ] || fail "rm_core on a missing network: expected exit 1, got $rc"
need_kv init -1 "RM_Init on a missing file"
need_not_grep '^survived=' "$WORK/last.out" "no work done after a failed RM_Init"
