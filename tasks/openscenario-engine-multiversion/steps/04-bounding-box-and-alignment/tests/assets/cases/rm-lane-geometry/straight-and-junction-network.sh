#!/usr/bin/env bash
# MERGED CASE. This file is two cases that used to live side by side in this
# group, a-lane-width-and-type.sh and b-junction-network.sh, folded into one
# file to stay inside the package's per-step case budget. Nothing was dropped:
# every assertion and every comment from both halves is below, in its original
# order, and each half still makes its own driver run against its own fixture.
# test.sh.in scores the GROUP, not the case file: `score` is the share-weighted
# fraction of the SCORED groups that passed, and the case count never enters
# that arithmetic -- so merging two files into one changes no weight and no
# denominator. What it does cost is localisation. A failure in either half now
# fails the whole file and therefore the whole group, and groups.txt's count
# column counts .sh files on disk, so this file is ONE counted case carrying
# the assertions of two.
#
# ---------------------------------------------------------------------------
# Half 1 (was a-lane-width-and-type.sh)
# ---------------------------------------------------------------------------
# INHERITED. Lane width and lane type, reached both by road id and through a
# position handle, must agree with each other and must not move across this
# step. Measured identical at v2.27.5 and v2.31.10.
source /tests/caselib.sh

run_driver rm_lane "$FIXTURES/straight_500m.xodr" 1 -1 250
need_status 0 $? "rm_lane on straight_500m.xodr"

need_kv init 0             "RM_Init on a valid .xodr"
need_kv nroads 1           "one road"
need_kv nlanes 2           "lane count at s=250"

need_close width_by_road 3.0700 0.0001  "RM_GetLaneWidthByRoadId"
need_kv type_by_road 2        "RM_GetLaneTypeByRoadId reports a driving lane"

# The handle-addressed spellings must return the same physical quantities as
# the road-id spellings; a solution where the two disagree has one of them wrong.
need_close width_handle 3.0700 0.0001   "RM_GetLaneWidth through a position handle"
need_kv type_handle 2         "RM_GetLaneType through a position handle"

need_kv pos_road 1         "road id read back"
need_kv pos_lane -1        "lane id read back"
need_close pos_s 250.000 0.001          "s read back"

# RM_SetLanePosition's success codes were widened upstream (2.21.2) so that 1
# and 2 also mean success; only a negative value is a failure. Assert the sign,
# never the exact code, or a correct solution that returns 1 is failed here.
set_rc=$(sed -n 's/^set_lane=//p' "$WORK/last.out" | tail -1)
[ -n "$set_rc" ] || fail "driver printed no set_lane= line"
[ "$set_rc" -ge 0 ] || fail "RM_SetLanePosition returned $set_rc; a non-negative code is required"

# ---------------------------------------------------------------------------
# Half 2 (was b-junction-network.sh)
# ---------------------------------------------------------------------------
# INHERITED. Same surface against fabriksgatan.xodr, a 16-road urban network
# with junctions, where the road count is the discriminator that a network was
# actually parsed rather than partially loaded. Measured identical at v2.27.5
# and v2.31.10.

run_driver rm_lane "$FIXTURES/fabriksgatan.xodr" 0 -1 10
need_status 0 $? "rm_lane on fabriksgatan.xodr"

need_kv init 0          "RM_Init on a valid .xodr"
need_kv nroads 16       "fabriksgatan.xodr has 16 roads"
need_kv nlanes 2        "lane count on road 0 at s=10"

need_close width_by_road 3.5000 0.0001  "RM_GetLaneWidthByRoadId"
need_kv type_by_road 2        "RM_GetLaneTypeByRoadId reports a driving lane"
need_close width_handle 3.5000 0.0001   "RM_GetLaneWidth through a position handle"
need_kv type_handle 2         "RM_GetLaneType through a position handle"

need_kv pos_road 0      "road id read back"
need_kv pos_lane -1     "lane id read back"
need_close pos_s 10.000 0.001           "s read back"

set_rc=$(sed -n 's/^set_lane=//p' "$WORK/last.out" | tail -1)
[ -n "$set_rc" ] || fail "driver printed no set_lane= line"
[ "$set_rc" -ge 0 ] || fail "RM_SetLanePosition returned $set_rc; a non-negative code is required"
