#!/usr/bin/env bash
# Gate (proof of life). The build must keep publishing the same six artifacts across this
# step. The frozen profile sets USE_OSG=False, so `replayer` and `odrviewer` are
# NOT built and must not be asserted here - requiring them would fail every
# correct solution. The published set is exactly: esmini, esmini-dyn, odrplot,
# dat2csv, libesminiLib.so, libesminiRMLib.so.
source /tests/caselib.sh

for t in "$ESMINI" "$ESMINI_DYN" "$ODRPLOT" "$DAT2CSV"; do
    [ -n "$t" ] || fail "the harness located no path for one of the four executables"
    [ -x "$t" ] || fail "not an executable: $t"
    file -b "$t" > "$WORK/file.out" 2>&1
    need_grep 'ELF .* executable' "$WORK/file.out" "$(basename "$t") is a native ELF executable"
done

# LIBDIR may be a colon-separated list: `cmake --install` does not always put
# the two shared objects in the same directory, and test.sh builds the variable
# accordingly. Split it rather than assuming one path.
find_lib() {
    local d dirs
    IFS=: read -r -a dirs <<< "$LIBDIR"
    for d in "${dirs[@]}"; do
        [ -f "$d/$1" ] && { printf '%s\n' "$d/$1"; return 0; }
    done
    return 1
}
for l in libesminiLib.so libesminiRMLib.so; do
    p=$(find_lib "$l") || fail "$l not found under LIBDIR=$LIBDIR"
    need_file "$p"
    # TEST_INTERFACE.md §2: the two shared libraries must be ELF shared objects,
    # and it says both `file -b` spellings are read. Assert it rather than
    # leaving the sentence laxer than the check.
    file -b "$p" > "$WORK/file.out" 2>&1
    need_grep 'ELF .* shared object' "$WORK/file.out" "$l is a native ELF shared object"
done

# The usage banners drift between releases (option ordering and wording both
# move), so match the stable spine only, never the whole block and never a line
# number. dat2csv's usage echoes argv[0], an absolute path that differs per
# machine, so it is deliberately not matched here at all.
#
# The spine matched below is frozen in writing, not guessed: instruction.md's
# "Exact output that must not change" states the `Usage: esmini [options]` line,
# the `Options: ` line, the two-space option indent with `--osc`, `--headless`
# and `--fixed_timestep` among the options, and odrplot's usage line including
# upstream's `ordplot` misspelling; WORKSPACE_HANDOFF.md repeats all of it under
# "What carries forward". The candidate is told these strings are inherited and
# that this step's new options join the list rather than replacing it.
run_esmini
need_grep '^Usage: esmini \[options\]' "$WORK/last.out" "esmini prints a usage banner"
need_grep '^  --osc <filename>'        "$WORK/last.out" "esmini documents --osc"
need_grep '^  --headless'              "$WORK/last.out" "esmini documents --headless"
need_grep '^  --fixed_timestep'        "$WORK/last.out" "esmini documents --fixed_timestep"

run_tool "$ODRPLOT"
need_grep '^Usage: ordplot openDriveFile\.xodr \[Output file, default=output\.csv\] \[Sampling_step, default=1\.0\]$' \
    "$WORK/last.out" "odrplot prints its usage banner, misspelling and defaults included"

# `file` says esmini-dyn is an ELF executable; that is not the same as saying it
# links against the library and can drive a scenario. The other three tools are
# each exercised for real above or elsewhere in this step, so exercise this one
# too: it is the library's own example player, so a build that published a
# binary linked against a broken or missing libesminiLib.so fails here rather
# than passing on a `file` check. param_cars.xosc is used rather than the
# braking fixture because it finishes in a fraction of the per-case budget at
# both ends of this step's range.
cp "$FIXTURES/param_cars.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"
run_tool "$ESMINI_DYN" --osc param_cars.xosc --headless --fixed_timestep 0.05
need_status 0 $? "esmini-dyn plays a scenario to its stop trigger"
