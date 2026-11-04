#!/usr/bin/env bash
# The ten entry points this step adds must be exported from the shared
# libraries under their own unmangled names, and the entry points it inherits
# must still be there. That is the C-linkage half of the published contract,
# and it is checked where the claim actually lives -- the dynamic symbol table
# -- rather than inferred from a C++ driver linking.
source /tests/caselib.sh

find_lib() {  # find_lib <soname>
    local d
    IFS=: read -r -a dirs <<< "$LIBDIR"
    for d in "${dirs[@]}"; do
        [ -f "$d/$1" ] && { printf '%s\n' "$d/$1"; return 0; }
    done
    return 1
}

rm_so=$(find_lib libesminiRMLib.so) || fail "libesminiRMLib.so not found under LIBDIR=$LIBDIR"
se_so=$(find_lib libesminiLib.so)   || fail "libesminiLib.so not found under LIBDIR=$LIBDIR"

nm -D --defined-only "$rm_so" > "$WORK/rm.sym" 2>"$WORK/rm.err" || fail "nm failed on $rm_so"
nm -D --defined-only "$se_so" > "$WORK/se.sym" 2>"$WORK/se.err" || fail "nm failed on $se_so"

want_exact() {  # want_exact <symfile> <name>
    # The name must appear on its own, in a T/W/B/D slot. A C++-mangled
    # spelling such as _Z16RM_GetLaneWidthii does not match, which is the whole
    # point of the check.
    awk -v n="$2" '$2 ~ /^[TWBDR]$/ && $3 == n { found = 1 } END { exit !found }' "$1" \
        || fail "$(basename "$1"): $2 is not exported with C linkage"
}

for s in RM_GetLaneWidth RM_GetLaneWidthByRoadId RM_GetLaneType RM_GetLaneTypeByRoadId \
         RM_SetRoadId RM_GetNumberOfRoadsOverlapping RM_GetOverlappingRoadId; do
    want_exact "$WORK/rm.sym" "$s"
done
for s in SE_ReportObjectWheelStatus SE_GetPauseFlag SE_SimpleVehicleSetMaxDeceleration; do
    want_exact "$WORK/se.sym" "$s"
done

# instruction.md section 8 names RM_PositionMoveForward and gives its signature.
# The symbol predates this step -- what this step changes is what it returns --
# but it is now part of the published contract and is checked alongside the rest.
want_exact "$WORK/rm.sym" RM_PositionMoveForward

# Nothing is removed from either header at this step. The export set is not
# enumerable from the documents in full, but every entry point that
# instruction.md's "What must keep working" lists BY NAME is, and the whole of
# that list is asserted here -- both libraries, not just the road manager.
# Without this, a solution that deleted an inherited export no case happens to
# call, or quietly dropped its C linkage while refactoring, would lose nothing.
for s in RM_Init RM_GetNumberOfRoads RM_GetIdOfRoadFromIndex RM_GetRoadLength \
         RM_GetRoadNumberOfLanes RM_CreatePosition RM_SetLanePosition \
         RM_SetWorldXYHPosition RM_GetPositionData RM_Close; do
    want_exact "$WORK/rm.sym" "$s"
done
for s in SE_Init SE_Step SE_Close SE_SimpleVehicleCreate \
         SE_SimpleVehicleControlBinary SE_SimpleVehicleGetState \
         SE_SimpleVehicleDelete; do
    want_exact "$WORK/se.sym" "$s"
done
