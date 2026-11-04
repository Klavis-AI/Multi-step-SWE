#!/usr/bin/env bash
# Requirement 18: the published C ABI finishes migrating indices and counts to
# unsigned types, grows two road-info structs, and drops four OSI entry points.
#
# Three kinds of evidence, because the three changes are observable in three
# different places:
#
#   - COMPILE TIME, through the abi_types driver. Its four function-pointer
#     initialisations only bind if the entry points are declared with exactly
#     the parameter and return types the requirement states: a signed `int`
#     where the release declares `unsigned int` is a different function type,
#     not a convertible one. A tree that has not made the migration fails to
#     BUILD the driver, which test.sh reports as COMPILE FAILED and which shows
#     up here as the driver being missing. Same for the two struct members,
#     which do not exist before the release appends them.
#   - RUN TIME, also through the driver: the added `road_type` / `road_rule`
#     members are pre-set to a sentinel and read back after a real query, so
#     declaring the members without filling them does not pass.
#   - THE DYNAMIC SYMBOL TABLE, for the four withdrawn OSI entry points. A
#     removal is only observable as an absence, and the linkage check in
#     TEST_INTERFACE.md only ever verifies that required symbols are PRESENT,
#     so the absence is checked directly here with `nm -D`.
#
# Measured at both ends: at v2.44.2 the driver does not compile at all (the
# permutation entry points are still `int`-based and neither struct has the two
# members) and all four OSI symbols are still exported.
source /tests/caselib.sh

########################### compile- and run-time ############################
run_driver abi_types "$FIXTURES/cruise_straight.xosc" "$FIXTURES/straight_500m.xodr" 1 -1 50.0 \
    || fail "abi_types exited $?"
need_kv survived 1 "driver ran to completion"

# The four migrated entry points bound at their declared types.
need_kv bound_type_migrated_entry_points 1 \
    "SE_GetNumberOfPermutations, SE_SelectPermutation, SE_GetRoadSign and SE_GetRoadSignValidityRecord carry their unsigned types"

# road_type and road_rule are the LAST two members of each struct, appended
# after the pre-existing `t`, not inserted into the middle of a published
# layout.
need_kv se_roadinfo_members_appended 1 "SE_RoadInfo gained road_type and road_rule as its last two members"
need_kv rm_lane_info_members_appended 1 "RM_RoadLaneInfo gained road_type and road_rule as its last two members"

# Both structs are actually FILLED by a real query, not merely declared. The
# driver pre-sets both members to -12345, so the values below are the library's
# own answers for this network: an unspecified OpenDRIVE type and the default
# right-hand rule both report 0.
need_kv rm_init 0 "RM_Init on the straight network"
need_kv rm_set_lane_ok 1 "RM_SetLanePosition returned >= 0"
need_kv rm_lane_info_rc 0 "RM_GetLaneInfo succeeded"
need_kv rm_road_type 0 "the road manager filled road_type"
need_kv rm_road_rule 0 "the road manager filled road_rule"
need_close rm_lane_width 3.07 0.001 "the pre-existing members of RM_RoadLaneInfo still read correctly"

need_kv se_init 0 "SE_Init on the cruise scenario"
need_kv se_road_info_rc 0 "SE_GetRoadInfoAtDistance succeeded"
need_kv se_road_type 0 "the scenario engine filled road_type"
need_kv se_road_rule 0 "the scenario engine filled road_rule"

# The count arrives through the unsigned-returning entry point. With no
# parameter distribution loaded there is nothing to permute.
need_kv permutations 0 "SE_GetNumberOfPermutations reports through its unsigned return"

######################### the four withdrawn OSI symbols #####################
# LIBDIR is a COLON-SEPARATED list in this project's layout, so it can never be
# pasted in front of a file name; each entry is tried in turn and a search
# rooted at the tree RESOURCES anchors us to is the fallback. This mirrors what
# cli-artifacts does for the same reason.
treeroot=$(dirname "$RESOURCES")
IFS=: read -r -a dirs <<< "$LIBDIR"
lib=""
for d in "${dirs[@]}"; do [ -f "$d/libesminiLib.so" ] && lib=$d/libesminiLib.so && break; done
[ -n "$lib" ] || lib=$(find "$treeroot" -name libesminiLib.so -not -path '*/externals/*' 2>/dev/null | head -1)
[ -n "$lib" ] || fail "libesminiLib.so not found under LIBDIR=$LIBDIR or under $treeroot"

nm -D --defined-only "$lib" > "$WORK/dynsyms.txt" 2>/dev/null \
    || fail "could not read the dynamic symbol table of $lib"
# Guard against an empty or unreadable table making the four absence checks
# below pass for the wrong reason: a symbol the release definitely still
# exports must be visible in the same listing first.
grep -Eq ' SE_Init$' "$WORK/dynsyms.txt" \
    || fail "the dynamic symbol table of $lib does not list SE_Init; the absence checks below would be vacuous"

for sym in SE_ClearOSIGroundTruth SE_UpdateOSIGroundTruth \
           SE_UpdateOSIStaticGroundTruth SE_UpdateOSIDynamicGroundTruth; do
    grep -Eq " $sym\$" "$WORK/dynsyms.txt" \
        && fail "$sym is still exported from libesminiLib.so; requirement 18 removes it"
done
exit 0
