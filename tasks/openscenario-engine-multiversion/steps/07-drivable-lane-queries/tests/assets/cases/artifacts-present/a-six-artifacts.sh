#!/usr/bin/env bash
# Gate (proof of life). The build must still produce the four executables and the two
# shared libraries the rest of the corpus drives.
#
# The frozen build profile sets USE_OSG=False, so odrviewer and replayer are NOT
# built and must not be asserted here. The usage banners of both esmini and
# esmini-dyn change inside this step's range, so only their tolerant shape is
# checked, never their exact text.
source /tests/caselib.sh

for tool in "$ESMINI" "$ESMINI_DYN" "$ODRPLOT" "$DAT2CSV"; do
    [ -x "$tool" ] || fail "not an executable: $tool"
    file -b "$tool" | grep -Eq 'ELF .* executable' || fail "not an ELF executable: $tool"
done

# LIBDIR may be a colon-separated list: the install step leaves the two shared
# libraries in different directories. Split it, then fall back to a tree search.
IFS=: read -r -a libdirs <<< "$LIBDIR"
for lib in libesminiLib.so libesminiRMLib.so; do
    found=""
    for d in "${libdirs[@]}"; do
        [ -f "$d/$lib" ] && { found="$d/$lib"; break; }
    done
    if [ -z "$found" ]; then
        for d in "${libdirs[@]}"; do
            hit=$(find "$d/.." -name "$lib" -type f 2>/dev/null | head -1)
            [ -n "$hit" ] && { found="$hit"; break; }
        done
    fi
    [ -n "$found" ] || fail "shared library not found in LIBDIR ($LIBDIR): $lib"
    file -b "$found" | grep -Eq 'ELF .* shared object' || fail "not an ELF shared object: $found"
done

# esmini must still describe its own interface and still advertise the two flags
# the rest of the corpus depends on.
run_esmini --help
need_grep 'Usage:.*\[options\]' "$WORK/last.out" "esmini usage line"
need_grep '\-\-osc <filename>' "$WORK/last.out" "esmini --osc flag"
need_grep '\-\-headless' "$WORK/last.out" "esmini --headless flag"

# --- entry-point completeness (R07.17) -----------------------------------
# Both shared libraries must export every entry point the corpus drivers call.
# A missing symbol fails at driver-compile time with a hard link error, but
# nm -D catches a symbol that was stripped or never linked into the .so even
# when the header still declares it.
#
# This list is deliberately restricted to symbols exported at BOTH bounding
# tags. It is a gate group, so every symbol named here must already be present
# at v2.46.4. The entry points this step ADDS must never be added to it: they do
# not exist at the start tag, so naming one here would fail the gate there and
# turn this step's headline capability into an unpriced veto instead of scored
# work. The new entry points are graded by the scored groups, which is where
# they belong.
SE_SYMS="SE_Init SE_Step SE_Close SE_GetSimulationTime SE_GetNumberOfObjects
SE_GetObjectState SE_GetDistanceToObject SE_SimpleGetDistanceToObject
SE_GetObjectOdometer SE_GetObjectVelocityGlobalXYZ SE_GetObjectAngularVelocity
SE_GetObjectAngularAcceleration SE_SetOptionValue SE_GetOptionValue
SE_GetOptionSet SE_SetOption SE_UnsetOption SE_GetOptionValuesCount
SE_StepDT SE_SimpleVehicleCreate SE_SimpleVehicleDelete
SE_SimpleVehicleControlBinary
SE_SimpleVehicleControlAnalog SE_SimpleVehicleGetState"

RM_SYMS="RM_Init RM_Close RM_CreatePosition RM_SetLanePosition RM_GetPositionData
RM_SetH RM_SetHMode RM_GetNumberOfRoads RM_GetIdOfRoadFromIndex RM_GetRoadLength
RM_GetLaneType RM_GetLaneTypeByRoadId RM_GetNumberOfRoadsOverlapping
RM_GetOverlappingRoadId RM_SetLogFilePath RM_SetWorldXYHPosition"

find_lib() {
    local libname="$1"
    local found=""
    for d in "${libdirs[@]}"; do
        [ -f "$d/$libname" ] && { found="$d/$libname"; break; }
    done
    if [ -z "$found" ]; then
        for d in "${libdirs[@]}"; do
            local hit
            hit=$(find "$d/.." -name "$libname" -type f 2>/dev/null | head -1)
            [ -n "$hit" ] && { found="$hit"; break; }
        done
    fi
    echo "$found"
}

L1=$(find_lib libesminiLib.so)
L2=$(find_lib libesminiRMLib.so)
[ -n "$L1" ] || fail "libesminiLib.so not found for nm check"
[ -n "$L2" ] || fail "libesminiRMLib.so not found for nm check"

SE_EXPORTED=$(nm -D --defined-only "$L1" | awk '{print $3}')
RM_EXPORTED=$(nm -D --defined-only "$L2" | awk '{print $3}')

for sym in $SE_SYMS; do
    echo "$SE_EXPORTED" | grep -qx "$sym" || \
        fail "libesminiLib.so does not export required entry point: $sym"
done

for sym in $RM_SYMS; do
    echo "$RM_EXPORTED" | grep -qx "$sym" || \
        fail "libesminiRMLib.so does not export required entry point: $sym"
done

exit 0
