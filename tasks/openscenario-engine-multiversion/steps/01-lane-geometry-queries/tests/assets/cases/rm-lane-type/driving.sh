#!/usr/bin/env bash
# RM_GetLaneType / RM_GetLaneTypeByRoadId return the OpenDRIVE lane type as the
# bit value of roadmanager::Lane::LaneType. DRIVING is 2.
source /tests/caselib.sh

run_driver rm_lane "$FIXTURES/straight_500m.xodr" 1 -1 250 || fail "rm_lane exited $?"
need_kv type_by_road 2 "RM_GetLaneTypeByRoadId on a driving lane"
need_kv type_handle 2 "RM_GetLaneType on a driving lane"

# Lane 0 reports `none` (1) regardless of the type declared for it in the file
# -- see instruction.md section 2, third paragraph. straight_500m.xodr declares
# type="driving" on its centre lane, which is exactly why this is the
# interesting case: a lookup that trusts the file's attribute answers 2 here
# and is wrong. The road manager normalizes the reference line's type on load.
run_driver rm_lane "$FIXTURES/straight_500m.xodr" 1 0 250 || fail "rm_lane exited $?"
need_kv type_by_road 1 "centre lane type"
need_kv type_handle 1 "centre lane type by handle"

# The negative half of the same contract. instruction.md section 2 states that
# a lane id the road does not have, a road id the network does not contain and
# a handle no setter has ever placed all report a NEGATIVE value -- every entry
# in the type table is positive, so the type query, unlike the width query, has
# room for a failure answer. The sign is asserted and the magnitude is not:
# the instruction promises "negative", so -2 is as correct as -1 and pinning
# one of them would score a choice the candidate was never given.
#
# rm_lane_absent parks its probe handle on a real lane (-1) of a real road
# before asking, so what fails below is the lookup and not the placement, and
# it asks the real lane too, so a build that answered negative to everything
# fails on present_type.
run_driver rm_lane_absent "$FIXTURES/straight_500m.xodr" 1 -1 250 -99 999 \
    || fail "rm_lane_absent exited $?"
need_kv nroads 1 "straight_500m has one road, id 1"
need_kv pos_lane -1 "the probe handle is parked on a real lane"
need_neg unplaced_type "RM_GetLaneType from a handle no setter has placed"
need_neg absent_lane_type_handle "RM_GetLaneType on a lane id the road does not have"
need_neg absent_lane_type_by_road "RM_GetLaneTypeByRoadId on a lane id the road does not have"
need_neg absent_road_type "RM_GetLaneTypeByRoadId on a road id the network does not contain"
need_kv present_type 2 "the real driving lane still answers in the same run"
need_kv survived 1 "none of the above brought the process down"
