#!/usr/bin/env bash
# New. The five RM_SetAlignMode* entry points must be gone from
# libesminiRMLib.so's dynamic symbol table, and the five mirrored
# SE_SetAlignMode* entry points from libesminiLib.so's (a driver that still
# called one would fail to compile against the TARGET build, which is
# backwards for a group that must fail at the START tag, so the removal itself
# is checked here, directly, against the .so files). The replacement is the
# position-mode bitmask family (RM_SetWorldPositionMode /
# RM_SetObjectPositionMode / RM_SetObjectPositionModeDefault), exercised by
# the driver below.
#
# Deliberately NOT asserted: z_after_objmode and z_back_on_road (the value the
# position settles on after RM_SetObjectPositionMode/...Default is applied).
# Those depend on mode-persistence and reset details beyond what this
# requirement (the removed symbol and its replacement's existence/behavior for
# absolute vs. relative Z) grades.
source /tests/caselib.sh

cp "$FIXTURES/curves_elevation.xodr" "$WORK/curves_elevation.xodr"

# LIBDIR is colon-separated in this project's build layout (the two libraries
# do not share a directory), so it is split rather than pasted into a path.
find_lib() {
    local lib="$1" d hit
    IFS=: read -r -a libdirs <<< "$LIBDIR"
    for d in "${libdirs[@]}"; do
        [ -f "$d/$lib" ] && { printf '%s\n' "$d/$lib"; return 0; }
    done
    for d in "${libdirs[@]}"; do
        hit=$(find "$d/.." -name "$lib" -type f 2>/dev/null | head -1)
        [ -n "$hit" ] && { printf '%s\n' "$hit"; return 0; }
    done
    return 1
}
rmlib=$(find_lib libesminiRMLib.so) || fail "libesminiRMLib.so not found in LIBDIR ($LIBDIR)"
selib=$(find_lib libesminiLib.so)   || fail "libesminiLib.so not found in LIBDIR ($LIBDIR)"
nm -D "$rmlib" > "$WORK/rm.syms" 2>/dev/null || fail "nm -D failed on $rmlib"
nm -D "$selib" > "$WORK/se.syms" 2>/dev/null || fail "nm -D failed on $selib"
# Sanity: the tables are the real export lists, not empty on a stripped or
# unreadable file, so an absent symbol below means removed rather than unread.
need_grep ' RM_GetPositionData$' "$WORK/rm.syms" "libesminiRMLib.so exports RM_GetPositionData"
need_grep ' SE_Init$' "$WORK/se.syms" "libesminiLib.so exports SE_Init"
for sym in RM_SetAlignMode RM_SetAlignModeH RM_SetAlignModeP RM_SetAlignModeR RM_SetAlignModeZ; do
    grep -Eq " ${sym}\$" "$WORK/rm.syms" && fail "$sym must be removed from libesminiRMLib.so"
done
for sym in SE_SetAlignMode SE_SetAlignModeH SE_SetAlignModeP SE_SetAlignModeR SE_SetAlignModeZ; do
    grep -Eq " ${sym}\$" "$WORK/se.syms" && fail "$sym must be removed from libesminiLib.so"
done

# The same removal paragraph (instruction R7, at instruction.md:147) says the
# removed family's replacement is the position-mode bitmask you add, and
# WORKSPACE_HANDOFF names which symbols that is on both sides: L40 ("the
# RM_PositionMode enum ... and RM_SetObjectPositionMode, ...") for the RM_ half
# and L32 ("SE_SetObjectPositionMode, SE_SetObjectPositionModeDefault") for the
# SE_ half. The two RM_* halves are exercised at runtime by
# d_removed below; the SE_* mirror of the same family has no runtime driver, so
# it is graded here off the export table it has to appear in. All of these are
# exported at the target and absent at the start tag, so this stays inside the
# scored groups.
for sym in RM_SetObjectPositionMode RM_SetObjectPositionModeDefault RM_SetWorldPositionMode; do
    grep -Eq " ${sym}\$" "$WORK/rm.syms" || fail "the replacement $sym must be exported by libesminiRMLib.so"
done
for sym in SE_SetObjectPositionMode SE_SetObjectPositionModeDefault; do
    grep -Eq " ${sym}\$" "$WORK/se.syms" || fail "the replacement $sym must be exported by libesminiLib.so"
done
# WORKSPACE_HANDOFF also lists these three as new entry points you must add --
# SE_GetRouteTotalLength and SE_GetObjectRouteStatus on L31, SE_ReportObjectPosMode
# on L33 -- and nothing else in the suite reaches them.
for sym in SE_GetRouteTotalLength SE_GetObjectRouteStatus SE_ReportObjectPosMode; do
    grep -Eq " ${sym}\$" "$WORK/se.syms" || fail "$sym must be exported by libesminiLib.so"
done

run_driver d_removed curves_elevation.xodr 1 50
need_status 0 $? "d_removed"

need_kv handle_ge0 1 "position handle created"
need_kv setlane_ge0 1 "lane position set"
need_close road_z -0.722 0.01 "road surface elevation at road 1, s=50"
need_close z_abs 7.000 0.001 "RM_Z_ABS honors the requested absolute z exactly"
need_close z_rel_minus_z_abs -0.722 0.01 "RM_Z_REL is measured from the road surface, offset by its elevation"
need_kv survived 1 "driver completed"

# The removal itself is checked above off the export tables. That catches a
# deleted symbol, but not one that was merely hidden from `nm -D` (a version
# script, -Wl,--exclude-libs, or a static archive left on the link line all hide
# a symbol while leaving it resolvable). instruction R7 promises more than
# absence: code that still calls a removed align-mode entry point "must fail to
# link". The compile-must-fail fixture in this step's fixtures directory is the
# only thing in the suite that calls one, against the public header, with no
# local redeclaration -- so it fails to compile exactly when the declaration is
# genuinely gone from the API. It is NOT named *.cpp, so the runner's driver
# glob never sees it.
# Both removed families are probed: the road-manager one through
# esminiRMLib.hpp and its scenario-engine mirror through esminiLib.hpp. The
# export-table checks above catch a deleted symbol; only these probes catch one
# that was hidden from `nm -D` while staying resolvable.
probe_once() {  # probe_once <fixture> <header name> <label>
    local probe="$FIXTURES/$1" hdr="$2" label="$3" inc="" d h
    need_file "$probe" "the compile-must-fail probe fixture ($label)"
    IFS=: read -r -a libdirs <<< "$LIBDIR"
    for d in "${libdirs[@]}"; do
        h=$(find "$d/.." -name "$hdr" -type f -not -path '*/externals/*' 2>/dev/null | head -1)
        [ -n "$h" ] && { inc=$(dirname "$h"); break; }
    done
    if [ -n "$inc" ] && command -v c++ >/dev/null 2>&1; then
        # -x c++ is required: the fixture's extension is deliberately not a
        # source extension, and without it the compiler treats the file as a
        # linker input and exits 0 without ever reading it -- a green that means
        # nothing.
        if c++ -std=c++17 -fsyntax-only -x c++ -I"$inc" "$probe" > "$WORK/probe.out" 2>&1; then
            fail "$label: code calling a removed align-mode symbol must fail to compile against the target headers ($inc)"
        fi
        # Reached only when the probe actually ran and correctly refused to
        # compile. Announced on stdout as well as recorded in the work
        # directory, so a grader reading only the case's output can tell the
        # build-dependent half ran rather than being skipped.
        echo "note: $label probe ran against $inc and failed to compile, as required"
        echo "note: $label probe ran against $inc and failed to compile, as required" > "$WORK/probe.ran.$label"
    else
        # The build-dependent half is skipped, not silently passed, when the
        # candidate's headers are not reachable from LIBDIR or no compiler is on
        # PATH: the export-table checks above still hold, and a compile probe
        # that cannot find the header would report a false failure for a layout
        # the runner does support.
        echo "note: $label probe SKIPPED, no header dir from LIBDIR=$LIBDIR or no c++ on PATH"
        echo "note: $label probe skipped" > "$WORK/probe.skipped.$label"
    fi
}

probe_once removed_align_mode_probe.cpp.txt   esminiRMLib.hpp rm
probe_once removed_align_mode_se_probe.cpp.txt esminiLib.hpp   se

exit 0
