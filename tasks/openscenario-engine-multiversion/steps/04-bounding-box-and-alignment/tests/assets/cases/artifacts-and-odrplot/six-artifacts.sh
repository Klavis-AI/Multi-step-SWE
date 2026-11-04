#!/usr/bin/env bash
# Gate (proof of life). The build must keep publishing the same six artifacts across
# this step. The frozen profile sets USE_OSG=False, so `replayer` and
# `odrviewer` are NOT built and must not be asserted here -- requiring them
# would fail every correct solution. The published set is exactly: esmini,
# esmini-dyn, odrplot, dat2csv, libesminiLib.so, libesminiRMLib.so.
source /tests/caselib.sh

for t in "$ESMINI" "$ESMINI_DYN" "$ODRPLOT" "$DAT2CSV"; do
    [ -n "$t" ] || fail "the harness located no path for one of the four executables"
    [ -x "$t" ] || fail "not an executable: $t"
    file -b "$t" > "$WORK/file.out" 2>&1
    need_grep 'ELF .* executable' "$WORK/file.out" "$(basename "$t") is a native ELF executable"
done

# LIBDIR may be a colon-separated list: the two shared objects do not always
# land in the same directory. Split it rather than assuming one path.
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
done

# The usage banners drift between releases in general, so match the stable
# spine only, never the whole block and never a line number.
run_esmini
need_grep '^Usage: esmini \[options\]' "$WORK/last.out" "esmini prints a usage banner"
need_grep '^  --osc <filename>'        "$WORK/last.out" "esmini documents --osc"
need_grep '^  --headless'              "$WORK/last.out" "esmini documents --headless"
need_grep '^  --fixed_timestep'        "$WORK/last.out" "esmini documents --fixed_timestep"

run_tool "$ODRPLOT"
need_grep 'openDriveFile\.xodr' "$WORK/last.out" "odrplot prints a usage banner"

# odrplot still RENDERS a network, not just prints its banner: it samples the
# road geometry of a .xodr into `track.csv` in the working directory, one
# `lane, ...` header per lane section followed by one sample row per step.
# Verified byte-identical between the two ends of this step on this fixture, so
# every value below is an inherited constant, not a new requirement.
cp "$FIXTURES/straight_500m.xodr" "$WORK/"
( cd "$WORK" && run_tool "$ODRPLOT" "$WORK/straight_500m.xodr" )
need_status 0 $? "odrplot on straight_500m.xodr"
need_grep '^Created track\.csv using stepsize 1\.00$' "$WORK/last.out" \
    "odrplot reports the network it rendered and its unchanged default stepsize"
need_file "$WORK/track.csv"
need_grep '^lane, 1, 0, 3, no-driving$' "$WORK/track.csv" \
    "the first lane section's header line is unchanged"
nsec=$(grep -c '^lane, ' "$WORK/track.csv")
[ "$nsec" -eq 7 ] || fail "odrplot emitted $nsec lane sections for straight_500m.xodr, not 7"
nrow=$(wc -l < "$WORK/track.csv")
[ "$nrow" -eq 3514 ] || fail "odrplot's rendering is $nrow lines, not the 3514 it has always been"

# The profile switches OpenSceneGraph off, so the two viewer executables are NOT
# built -- the instruction's Scope says so in as many words. Nothing else in
# this step asserts the absence of anything the candidate might have added.

# Check the PUBLISHED set first: the directories the six artifacts above
# actually landed in, wherever the candidate chose to put them. A viewer target
# the candidate built would be published beside the rest of its output, so this
# half of the check does not depend on where the build tree sits relative to
# $RESOURCES.
pubdirs=$(
    for t in "$ESMINI" "$ESMINI_DYN" "$ODRPLOT" "$DAT2CSV"; do
        [ -n "$t" ] && dirname "$t"
    done
    IFS=: read -r -a libdirs <<< "$LIBDIR"
    for d in "${libdirs[@]}"; do [ -n "$d" ] && printf '%s\n' "$d"; done
)
for d in $(printf '%s\n' $pubdirs | sort -u); do
    for forbidden in replayer odrviewer; do
        if [ -x "$d/$forbidden" ]; then
            fail "$forbidden was published alongside the six artifacts ($d/$forbidden); USE_OSG is off and the Scope forbids it"
        fi
    done
done

# Then sweep the tree as well, which catches a viewer left in a build directory
# it was never installed out of. Bounded at six levels below $RESOURCES' parent,
# so it is a second net under the published-set check above, not the only one.
app=$(dirname "$RESOURCES")
for forbidden in replayer odrviewer; do
    extra=$(find "$app" -maxdepth 6 -name "$forbidden" -type f -perm -u+x \
                -not -path '*/externals/*' 2>/dev/null | head -1)
    [ -z "$extra" ] || fail "$forbidden was built ($extra); USE_OSG is off and the Scope forbids it"
done
