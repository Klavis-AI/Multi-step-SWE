#!/usr/bin/env bash
# The entry points this step adds must be exported under their own unmangled
# names -- the C-linkage half of the published contract -- checked where the
# claim actually lives, the dynamic symbol table, rather than inferred from a
# C++ driver linking against the header.
source /tests/caselib.sh

find_lib() {  # find_lib <soname>
    local d dirs
    IFS=: read -r -a dirs <<< "$LIBDIR"
    for d in "${dirs[@]}"; do
        [ -f "$d/$1" ] && { printf '%s\n' "$d/$1"; return 0; }
    done
    return 1
}

se_so=$(find_lib libesminiLib.so) || fail "libesminiLib.so not found under LIBDIR=$LIBDIR"
rm_so=$(find_lib libesminiRMLib.so) || fail "libesminiRMLib.so not found under LIBDIR=$LIBDIR"
nm -D --defined-only "$se_so" > "$WORK/se.sym" 2>"$WORK/se.err" || fail "nm failed on $se_so"
nm -D --defined-only "$rm_so" > "$WORK/rm.sym" 2>"$WORK/rm.err" || fail "nm failed on $rm_so"

want_exact() {  # want_exact <symfile> <libname> <name>
    # The name must appear on its own, in a T/W/B/D/R slot. A C++-mangled
    # spelling such as _Z16SE_SetAlignModeZii does not match, which is the
    # whole point of the check.
    awk -v n="$3" '$2 ~ /^[TWBDR]$/ && $3 == n { found = 1 } END { exit !found }' "$1" \
        || fail "$3 is not exported from $2 with C linkage"
}

# The alignment, bounding-box and distance additions to libesminiLib.so.
for s in SE_AddObjectWithBoundingBox SE_SetAlignMode SE_SetAlignModeH \
         SE_SetAlignModeP SE_SetAlignModeR SE_SetAlignModeZ \
         SE_SetSnapLaneTypes SE_GetDistanceToObject; do
    want_exact "$WORK/se.sym" libesminiLib.so "$s"
done

# The one addition to libesminiRMLib.so.
want_exact "$WORK/rm.sym" libesminiRMLib.so RM_SetSnapLaneTypes

# The published surface also SHRINKS at this step. That half of the contract is
# graded by its own group, cases/abi-removed-symbols/, so that a tree which adds
# the nine names but keeps the withdrawn one is distinguishable from one that
# followed the release through.
