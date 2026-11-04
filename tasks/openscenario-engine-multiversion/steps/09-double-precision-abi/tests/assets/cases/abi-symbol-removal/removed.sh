#!/usr/bin/env bash
# Widening SE_GetSimulationTime to double makes SE_GetSimulationTimeDouble a
# duplicate of it, and the step says this is a widening of existing entry points
# rather than a parallel set. So the float-era companion must be gone from the
# exported surface, not merely left unused - a build that keeps it has not
# migrated, it has added. SE_UpdateOSITrafficCommand goes with it.
#
# Absence is asserted against the dynamic symbol table, which is where a caller
# would find them, and only after confirming that the table is readable and
# populated - an unreadable or empty table would make an absence check pass
# vacuously.
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

# The table is real: the entry point that replaces the removed one is in it.
awk '$2 ~ /^[TWBDR]$/ && $3 == "SE_GetSimulationTime" { found = 1 } END { exit !found }' "$WORK/se.sym" \
    || fail "SE_GetSimulationTime is not exported - the symbol table check would be vacuous"
n=$(grep -c ' SE_' "$WORK/se.sym")
[ "$n" -ge 50 ] || fail "only $n SE_ symbols exported; the table looks truncated"

want_absent() {  # want_absent <name>
    awk -v n="$1" '$2 ~ /^[TWBDR]$/ && $3 == n { found = 1 } END { exit found }' "$WORK/se.sym" \
        || fail "$1 is still exported from libesminiLib.so"
}

want_absent SE_GetSimulationTimeDouble
want_absent SE_UpdateOSITrafficCommand
