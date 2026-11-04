#!/usr/bin/env bash
# instruction.md requirement 7 ends with "The entry point is exported from
# libesminiLib.so under its own unmangled name, like every other SE_ entry
# point", and TEST_INTERFACE.md section 3 says that claim is checked by reading
# the dynamic symbol table. This case is that check.
#
# The C++ driver in this group cannot stand in for it: it includes the
# candidate's own header, so a declaration without extern "C" produces a
# C++-mangled export that the driver still compiles and links against happily.
# The mangling is only visible here.
source /tests/caselib.sh

find_lib() {  # find_lib <soname>
    local d
    IFS=: read -r -a dirs <<< "$LIBDIR"
    for d in "${dirs[@]}"; do
        [ -f "$d/$1" ] && { printf '%s\n' "$d/$1"; return 0; }
    done
    return 1
}

se_so=$(find_lib libesminiLib.so) || fail "libesminiLib.so not found under LIBDIR=$LIBDIR"
nm -D --defined-only "$se_so" > "$WORK/se.sym" 2>"$WORK/se.err" || fail "nm failed on $se_so"

want_exact() {  # want_exact <name>
    # The name must appear on its own, in a T/W/B/D/R slot. A C++-mangled
    # spelling such as _Z25SE_GetRoadInfoAlongRouteifP11SE_RoadInfoib does not
    # match, which is the whole point of the check.
    awk -v n="$1" '$2 ~ /^[TWBDR]$/ && $3 == n { found = 1 } END { exit !found }' "$WORK/se.sym" \
        || fail "$1 is not exported from libesminiLib.so with C linkage"
}

# The entry point this step adds.
want_exact SE_GetRoadInfoAlongRoute
# The lookahead it sits beside is still exported the same way: requirement 7 is
# a widening, not a replacement.
want_exact SE_GetRoadInfoAtDistance

# ---------------------------------------------------------------------------
# The other half of the ABI this step ships is invisible to nm.
#
# `nm -D` lists NAMES. Everything below keeps the name it already had and
# changes its SHAPE: SE_AddObject and SE_AddObjectWithBoundingBox gain a
# model_3d parameter, and SE_RoadInfo and RM_RoadLaneInfo gain a lane_type
# member. Under C linkage none of that reaches the symbol table - the two
# entry points appear as bare `SE_AddObject` / `SE_AddObjectWithBoundingBox`
# at both ends of this step - so the only instrument that can see it is a
# compile against the candidate's own headers. That is what these two drivers
# are: a tree carrying the narrower declarations fails to build them, and
# run_driver then reports the 127 of a binary that does not exist.
# ---------------------------------------------------------------------------

want_exact SE_AddObject
want_exact SE_AddObjectWithBoundingBox

# Object injection with the model_3d argument. The fixture starts with two
# entities; each successful add returns the new object's id and raises the
# count, so the ids and the count cross-check each other.
run_driver se_objects "$FIXTURES/two_car_brake.xosc"
need_kv init 0 "se_objects init"
need_kv nobj_before 2 "fixture entity count before any add"
need_kv add_id 2 "SE_AddObject with model_3d returns the new object id"
need_kv add_bb_id 3 "SE_AddObjectWithBoundingBox with model_3d returns the new object id"
need_kv nobj_after 4 "both added objects are in the scenario"
need_kv survived 1 "se_objects reached the end"

# The lane-type member of the lookahead result. The scenario drives in lane -1
# of a driving lane, so the reported type is LANE_TYPE_DRIVING = 1 << 1.
need_nonneg ri_rc "SE_GetRoadInfoAtDistance succeeds"
need_kv ri_lane -1 "lookahead lane id"
need_kv ri_lane_type 2 "SE_RoadInfo.lane_type reports DRIVING"

# The same member on the road-manager side, cross-checked against
# RM_GetLaneTypeByRoadId - the pre-existing query for the same fact. The new
# field has to AGREE with the old one, not merely exist.
run_driver rm_lane_type "$FIXTURES/straight_500m.xodr" 1 -1 250
need_kv init 0 "rm_lane_type init on the driving lane"
need_kv set_lane_ok 1 "the probe position was placed on road 1 lane -1"
need_nonneg lane_info_rc "RM_GetLaneInfo succeeds"
need_nonneg probe_rc "RM_GetProbeInfo succeeds"
need_kv type_by_road 2 "RM_GetLaneTypeByRoadId says DRIVING for lane -1"
need_kv info_lane -1 "lane info is about lane -1"
need_kv info_lane_type 2 "RM_RoadLaneInfo.lane_type reports DRIVING"
need_kv probe_lane_type 2 "RM_RoadProbeInfo carries the same lane type"
need_kv survived 1 "rm_lane_type reached the end on the driving lane"

# A lane that is NOT driving, so the field is carrying the lane's real type and
# not a constant: lane -2 of the same road is a border lane, 1 << 6.
run_driver rm_lane_type "$FIXTURES/straight_500m.xodr" 1 -2 250
need_kv set_lane_ok 1 "the probe position was placed on road 1 lane -2"
need_kv type_by_road 64 "RM_GetLaneTypeByRoadId says BORDER for lane -2"
need_kv info_lane -2 "lane info is about lane -2"
need_kv info_lane_type 64 "RM_RoadLaneInfo.lane_type reports BORDER"
need_kv survived 1 "rm_lane_type reached the end on the border lane"

exit 0
