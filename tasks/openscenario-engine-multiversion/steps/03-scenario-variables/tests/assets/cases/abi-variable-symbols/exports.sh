#!/usr/bin/env bash
# The ten variable entry points must be exported from the scenario-engine
# shared library under their own unmangled names. That is the C-linkage half of
# the published contract, and it is checked where the claim lives -- the
# dynamic symbol table -- rather than inferred from a C++ driver linking.
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

want_exact() {  # want_exact <symfile> <name>
    # The name must appear on its own in a T/W/B/D/R slot. A C++-mangled
    # spelling such as _Z16SE_GetVariableIntPKcPi does not match, which is the
    # whole point of the check.
    awk -v n="$2" '$2 ~ /^[TWBDR]$/ && $3 == n { found = 1 } END { exit !found }' "$1" \
        || fail "$(basename "$1"): $2 is not exported with C linkage"
}

for s in SE_GetVariableInt SE_GetVariableDouble SE_GetVariableString SE_GetVariableBool \
         SE_SetVariableInt SE_SetVariableDouble SE_SetVariableString SE_SetVariableBool \
         SE_GetVariable SE_SetVariable; do
    want_exact "$WORK/se.sym" "$s"
done

# The parameter entry points they sit beside are still exported too: this step
# adds a namespace, it does not replace one.
for s in SE_GetParameterInt SE_GetParameterDouble SE_GetParameterString SE_GetParameterBool \
         SE_SetParameterInt SE_SetParameterDouble SE_SetParameterString SE_SetParameterBool \
         SE_GetParameter SE_SetParameter; do
    want_exact "$WORK/se.sym" "$s"
done

# Nothing is removed at this step, so the rest of the inherited published
# surface is enumerated here too rather than left to whichever calls the gate
# drivers happen to make: a dropped entry point that no driver calls would
# otherwise go uncharged. Every name below is one instruction.md §7 states
# stays exported under its own unmangled name.
for s in SE_Init SE_InitWithArgs SE_Step SE_StepDT SE_GetSimulationTimeDouble \
         SE_GetObjectState SE_Close SE_GetIdByName \
         SE_GetNumberOfParameters SE_GetParameterName \
         SE_SetParameterDistribution SE_GetNumberOfPermutations SE_SelectPermutation \
         SE_GetPermutationIndex SE_ResetParameterDistribution; do
    want_exact "$WORK/se.sym" "$s"
done

# The road manager is the other half of that surface, and it is a second
# library with its own symbol table.
rm_so=$(find_lib libesminiRMLib.so) || fail "libesminiRMLib.so not found under LIBDIR=$LIBDIR"
nm -D --defined-only "$rm_so" > "$WORK/rm.sym" 2>"$WORK/rm.err" || fail "nm failed on $rm_so"

for s in RM_Init RM_Close RM_CreatePosition RM_SetLanePosition RM_GetPositionData \
         RM_GetNumberOfRoads RM_GetRoadNumberOfLanes \
         RM_GetLaneWidth RM_GetLaneWidthByRoadId RM_GetLaneType RM_GetLaneTypeByRoadId; do
    want_exact "$WORK/rm.sym" "$s"
done
