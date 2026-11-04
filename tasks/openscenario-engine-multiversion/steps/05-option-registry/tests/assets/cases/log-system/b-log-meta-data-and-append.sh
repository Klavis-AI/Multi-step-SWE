#!/usr/bin/env bash
# New. log_meta_data is OFF by default (a [file::func::line] tag is only added
# when explicitly requested), and log_append controls whether a log file
# written via --logfile_path is truncated or grown across successive runs.
source /tests/caselib.sh

cp "$FIXTURES/elev_cruise.xosc" "$WORK/elev_cruise.xosc"
cp "$FIXTURES/curves_elevation.xodr" "$WORK/curves_elevation.xodr"

# The documented metadata tag is `[<file>::<function>::<line>]` placed after
# the level tag and before the message. No particular file or function name is
# asserted -- only the tag's shape, and its absence by default.
meta='\[[A-Za-z0-9_.-]+::[A-Za-z0-9_~]+::[0-9]+\]'
run_esmini --osc elev_cruise.xosc --headless --fixed_timestep 0.05 --disable_log
need_status 0 $? "esmini (default log_meta_data)"
need_not_grep "$meta" "$WORK/last.out" "log_meta_data is off by default"

run_esmini --osc elev_cruise.xosc --headless --fixed_timestep 0.05 --disable_log --log_meta_data
need_status 0 $? "esmini --log_meta_data"
need_grep "^\[([0-9]+\.[0-9]{3})?\] \[[a-z]+\] $meta " "$WORK/last.out" "log_meta_data inserts a file::function::line tag between the level and the message"

run_esmini --osc elev_cruise.xosc --headless --fixed_timestep 0.05 --logfile_path plain.log
need_status 0 $? "esmini --logfile_path (first run, no log_append)"
n1=$(wc -l < "$WORK/plain.log")
run_esmini --osc elev_cruise.xosc --headless --fixed_timestep 0.05 --logfile_path plain.log
need_status 0 $? "esmini --logfile_path (second run, no log_append)"
n2=$(wc -l < "$WORK/plain.log")
[ "$n1" -eq "$n2" ] || fail "without --log_append the log file must be truncated each run: $n1 then $n2"

# R05.14: a third run against the same --logfile_path is still the truncating
# form. Measured rather than assumed -- the file must not have grown, and it
# must still be non-empty (a build that "truncates" by writing nothing is not
# the documented behaviour either).
run_esmini --osc elev_cruise.xosc --headless --fixed_timestep 0.05 --logfile_path plain.log
need_status 0 $? "esmini --logfile_path (third run, no log_append)"
n3=$(wc -l < "$WORK/plain.log")
[ "$n3" -eq "$n1" ] || fail "a later run against the same --logfile_path must still truncate: $n1 then $n3"
[ "$n3" -gt 0 ] || fail "the log file must not be empty after a run that logged to it"

run_esmini --osc elev_cruise.xosc --headless --fixed_timestep 0.05 --logfile_path append.log --log_append
need_status 0 $? "esmini --logfile_path --log_append (first run)"
a1=$(wc -l < "$WORK/append.log")
run_esmini --osc elev_cruise.xosc --headless --fixed_timestep 0.05 --logfile_path append.log --log_append
need_status 0 $? "esmini --logfile_path --log_append (second run)"
a2=$(wc -l < "$WORK/append.log")
[ "$a2" -gt "$a1" ] || fail "--log_append must grow the log file across runs: $a1 then $a2"

# R05.14 also names the entry point that carries the truncation obligation:
# `SE_CloseLogFile` is new at this release and must be exported by
# libesminiLib.so. Asserted by name in the export table, the same weakest form
# this package uses for the other presence-only entry points -- the group has
# no driver budget for a runtime call.
#
# LIBDIR is colon-separated in this project's build layout (the two libraries
# are installed into different directories), so it is split rather than pasted
# into a path.
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
selib=$(find_lib libesminiLib.so) || fail "libesminiLib.so not found in LIBDIR ($LIBDIR)"
nm -D "$selib" > "$WORK/se.syms" 2>/dev/null || fail "nm -D failed on $selib"
# Sanity: the table is the real export list, not empty on a stripped or
# unreadable file, so the absent-symbol check below means what it says.
need_grep ' SE_Init$' "$WORK/se.syms" "libesminiLib.so exports SE_Init"
need_grep ' SE_CloseLogFile$' "$WORK/se.syms" "libesminiLib.so exports SE_CloseLogFile (R05.14)"

exit 0
