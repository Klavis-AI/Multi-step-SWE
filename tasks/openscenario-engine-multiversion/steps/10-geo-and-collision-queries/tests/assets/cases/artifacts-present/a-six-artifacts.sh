#!/usr/bin/env bash
# Gate (proof of life). The six published artefacts -- four executables and two shared
# libraries -- exist, are the right kind of ELF file, run, and still export the
# embedding and road-manager entry points that existed before this step.
#
# Only symbols that are present at both ends of the step are checked here; the
# newly exported ones are the business of the groups that exercise them.
source /tests/caselib.sh

for b in "$ESMINI" "$ESMINI_DYN" "$ODRPLOT" "$DAT2CSV"; do
    [ -x "$b" ] || fail "not an executable: $b"
    file -b "$b" | grep -q 'ELF .* executable' || fail "not an ELF executable: $b"
done

IFS=: read -r -a dirs <<< "$LIBDIR"
find_lib() {
    local so="$1" d
    for d in "${dirs[@]}"; do [ -f "$d/$so" ] && { printf '%s\n' "$d/$so"; return 0; }; done
    return 1
}

SELIB=$(find_lib libesminiLib.so)   || fail "libesminiLib.so not found under LIBDIR=$LIBDIR"
RMLIB=$(find_lib libesminiRMLib.so) || fail "libesminiRMLib.so not found under LIBDIR=$LIBDIR"
for so in "$SELIB" "$RMLIB"; do
    file -b "$so" | grep -q 'ELF .* shared object' || fail "not an ELF shared object: $so"
done

# Exported with C linkage, so an embedder can dlsym them by their plain names.
has_sym() {
    nm -D --defined-only "$1" 2>/dev/null \
        | awk -v n="$2" '$2 ~ /^[TWBDR]$/ && $3 == n { found = 1 } END { exit !found }'
}
for s in SE_Init SE_Close SE_StepDT SE_GetObjectState SE_GetSimulationTime \
         SE_CollisionDetection SE_GetObjectNumberOfCollisions; do
    has_sym "$SELIB" "$s" || fail "libesminiLib.so does not export $s"
done
for s in RM_Init RM_Close RM_CreatePosition RM_GetNumberOfRoads \
         RM_SetLanePosition RM_GetPositionData; do
    has_sym "$RMLIB" "$s" || fail "libesminiRMLib.so does not export $s"
done

# The player still documents itself. Its exit status for --help is left
# unasserted: it differs between tools and is not what this case is about.
run_tool "$ESMINI" --help
need_grep '^Usage: esmini \[options\]' "$WORK/last.out" "esmini --help usage line"
need_grep '^  \[?--osc\]? <filename>' "$WORK/last.out" "esmini --help scenario option"
need_grep '\-\-headless' "$WORK/last.out" "esmini --help lists --headless"
need_grep '\-\-fixed_timestep' "$WORK/last.out" "esmini --help lists --fixed_timestep"
need_grep '\-\-record' "$WORK/last.out" "esmini --help lists --record"
need_grep '\-\-csv_logger' "$WORK/last.out" "esmini --help lists --csv_logger"

# The shared-library-driven player starts, identifies its build and explains
# its own usage when given nothing to run.
run_tool "$ESMINI_DYN"
need_grep 'esmini BUILD VERSION' "$WORK/last.out" "esmini-dyn identifies itself"
need_grep '^Usage variant 1: esmini-dyn <osc filename>' "$WORK/last.out" "esmini-dyn usage"

run_tool "$ODRPLOT"
need_grep '^Usage: ordplot openDriveFile\.xodr' "$WORK/last.out" "odrplot usage"

run_tool "$DAT2CSV"
need_grep '^Usage: dat2csv \[options\]' "$WORK/last.out" "dat2csv usage"

# ... and it still lists the eight options it had at the previous step. The
# pattern tolerates the bracketed spelling the tool prints for its positional
# option ("  [--file] <filename>"). What each name is matched against is the
# name and nothing more, because the names are what the documents pin: no
# document says whether a flag-only option's line ends in a space, so requiring
# one would fail a conforming printer that does not write it. `--file` is the
# one name that needs more, and it gets it: it is a prefix of `--file_refs`, so
# its check requires a space or a '<' after it, which is also what its own
# documented entry ("[--file] <filename>") carries -- the eight names are all
# distinct beyond that prefix pair, so no other line can stand in for a
# missing one. The decoder's option set is inherited surface, not this step's
# addition: --precision is checked where it is introduced, in the dat-precision
# group.
for opt in file_refs csv extended path print_csv help version; do
    need_grep "^  \[?--$opt\]?" "$WORK/last.out" \
        "dat2csv still lists --$opt"
done
need_grep "^  \[?--file\]?[[:space:]<]" "$WORK/last.out" \
    "dat2csv still lists --file"

# The scenario and network libraries the player ships with are still there.
[ -d "$RESOURCES/xodr" ] || fail "resources/xodr is missing"
[ -d "$RESOURCES/xosc" ] || fail "resources/xosc is missing"
