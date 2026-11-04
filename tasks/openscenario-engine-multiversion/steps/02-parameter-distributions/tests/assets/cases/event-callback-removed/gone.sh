#!/usr/bin/env bash
# SE_RegisterEventCallback is retired in favour of
# SE_RegisterStoryBoardElementStateChangeCallback (see storyboard-callback):
# it must no longer be part of libesminiLib.so's exported, C-linkage surface,
# and nothing built against it may resolve. Checked directly against the
# dynamic symbol table and against whether a driver source calling the old
# name was able to compile and link at all, rather than by running anything -
# there is nothing left to run.
source /tests/caselib.sh

# LIBDIR may be a colon-separated list of directories -- the two shared objects
# do not always land in the same place -- so split it instead of indexing into
# it as though it were a single path.
find_lib() {
    local d dirs
    IFS=: read -r -a dirs <<< "$LIBDIR"
    for d in "${dirs[@]}"; do
        [ -f "$d/$1" ] && { printf '%s\n' "$d/$1"; return 0; }
    done
    return 1
}
so=$(find_lib libesminiLib.so) || fail "libesminiLib.so not found under LIBDIR=$LIBDIR"
need_file "$so"

nm -D --defined-only "$so" > "$WORK/syms.txt" 2>"$WORK/nm.err" || fail "nm failed on libesminiLib.so"

if awk '$2 ~ /^[TWBDR]$/ && $3 == "SE_RegisterEventCallback" { found = 1 } END { exit !found }' "$WORK/syms.txt"
then
    fail "SE_RegisterEventCallback is still exported with C linkage; it must be removed"
fi

# A driver source that only calls the retired entry point (drivers/se_event_callback.cpp)
# is compiled by the harness alongside every other driver. If the symbol is
# genuinely gone, that compile must fail and no executable appears here.
if [ -x "$DRIVERS/se_event_callback" ]
then
    fail "a driver calling SE_RegisterEventCallback compiled and linked; the symbol must be gone"
fi

exit 0
