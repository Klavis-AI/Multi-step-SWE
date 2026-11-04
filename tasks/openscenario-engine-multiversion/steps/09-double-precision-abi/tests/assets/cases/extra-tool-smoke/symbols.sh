#!/usr/bin/env bash
# The entry points whose signatures changed are still exported from the shared
# libraries under their own unmangled names. The type of what they take is a
# compile-time property the drivers assert on; this case pins the other half --
# that they remain ordinary C symbols rather than acquiring C++ mangling.
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
    awk -v n="$2" '$2 ~ /^[TWBDR]$/ && $3 == n { found = 1 } END { exit !found }' "$1" \
        || fail "$(basename "$1"): $2 is not exported with C linkage"
}

for s in RM_GetLaneWidth RM_GetLaneWidthByRoadId RM_GetRoadLength RM_GetSpeedLimit \
         RM_SetWorldXYHPosition RM_GetPositionData RM_SetLanePosition; do
    want_exact "$WORK/rm.sym" "$s"
done
for s in SE_GetSimulationTime SE_GetSimTimeStep SE_ReportObjectPos \
         SE_ReportObjectSpeed SE_ReportObjectWheelStatus SE_ReportObjectRoadPos \
         SE_GetObjectState; do
    want_exact "$WORK/se.sym" "$s"
done

# None of the changed entry points may have acquired a C++ mangled spelling.
for s in SE_ReportObjectPos RM_GetLaneWidth SE_GetSimulationTime; do
    if grep -q "_Z.*$s" "$WORK/rm.sym" "$WORK/se.sym" 2>/dev/null; then
        fail "$s also appears C++-mangled"
    fi
done
