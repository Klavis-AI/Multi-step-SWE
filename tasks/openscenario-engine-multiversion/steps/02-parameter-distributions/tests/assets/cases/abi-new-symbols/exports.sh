#!/usr/bin/env bash
# The ten entry points this step adds to the scenario-engine library must be
# exported from libesminiLib.so under their own unmangled names. That is the
# C-linkage half of the published contract, and it is checked where the claim
# actually lives -- the dynamic symbol table -- rather than inferred from a C++
# driver linking against the header.
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
    # spelling such as _Z24SE_SetParameterDistributionPKc does not match, which
    # is the whole point of the check.
    awk -v n="$1" '$2 ~ /^[TWBDR]$/ && $3 == n { found = 1 } END { exit !found }' "$WORK/se.sym" \
        || fail "$1 is not exported from libesminiLib.so with C linkage"
}

# The parameter-distribution surface.
for s in SE_SetParameterDistribution SE_ResetParameterDistribution \
         SE_GetNumberOfPermutations SE_SelectPermutation SE_GetPermutationIndex; do
    want_exact "$s"
done
# The rest of this release's additions to the same library.
for s in SE_SetDatFilePath SE_GetIdByName SE_GetSimulationTimeDouble \
         SE_RegisterStoryBoardElementStateChangeCallback SE_SetOffScreenRendering; do
    want_exact "$s"
done
