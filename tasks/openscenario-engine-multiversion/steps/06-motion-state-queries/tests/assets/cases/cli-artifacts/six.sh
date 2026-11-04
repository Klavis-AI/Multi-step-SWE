#!/usr/bin/env bash
# Requirement 16: the artifacts this step's build profile publishes exist, are
# the right kind of file, and run.
#
# The six are exactly the ones requirement 16 lists - the executables esmini,
# esmini-dyn, odrplot and dat2csv and the shared libraries libesminiLib.so and
# libesminiRMLib.so - with the same exclusions requirement 16 states and for
# the same reasons.
source /tests/caselib.sh

for b in "$ESMINI" "$ESMINI_DYN" "$ODRPLOT" "$DAT2CSV"; do
    [ -x "$b" ] || fail "not an executable: $b"
    file -b "$b" | grep -q 'ELF .* executable' || fail "not an ELF executable: $b"
done

# LIBDIR is documented as "the" directory holding both shared libraries, but
# TEST_INTERFACE.md only promises each artifact exists somewhere under the
# built tree, and this project's own layout puts libesminiRMLib.so outside the
# install directory. Check every LIBDIR entry first, then fall back to a
# search rooted at the tree RESOURCES already anchors us to.
treeroot=$(dirname "$RESOURCES")
IFS=: read -r -a dirs <<< "$LIBDIR"
for so in libesminiLib.so libesminiRMLib.so; do
    found=""
    for d in "${dirs[@]}"; do [ -f "$d/$so" ] && found=$d/$so && break; done
    [ -n "$found" ] || found=$(find "$treeroot" -name "$so" -not -path '*/externals/*' 2>/dev/null | head -1)
    [ -n "$found" ] || fail "$so not found under LIBDIR=$LIBDIR or under $treeroot"
    file -b "$found" | grep -q 'ELF .* shared object' || fail "not an ELF shared object: $found"
done

# The usage banner is matched tolerantly on purpose: upstream stops filling in
# the program name during this step ("Usage: esmini [options]" at v2.44.2 vs
# "Usage:  [options]" at v2.46.4), and a gate obligation must hold at
# both ends. esmini exits non-zero after printing help, so the status is not
# asserted here.
run_tool "$ESMINI" --help
need_grep '^Usage:.*\[options\]' "$WORK/last.out" "esmini --help usage line"
need_grep '\-\-osc <filename>' "$WORK/last.out" "esmini --help option list"
need_grep '\-\-headless' "$WORK/last.out" "esmini --help lists the headless option"

# esmini-dyn gets the same tolerant treatment as esmini above, and for the same
# reason: requirement 16 asks that the artifact be present, be the right kind of
# file, and RUN. It does not say what esmini-dyn's exit status is when no
# scenario is named, and the sibling tool documents the opposite convention for
# that case (requirement 15: dat2csv with no arguments exits 255), so the status
# is not pinned here - only that the tool did not hang. Nor is the program name
# in the usage line pinned; upstream stops filling argv[0] into esmini's banner
# during this step, and nothing promises esmini-dyn will not follow.
run_tool "$ESMINI_DYN"
st=$?
[ "$st" -ne 124 ] || fail "esmini-dyn with no arguments did not terminate within $CASE_TIMEOUT s"
need_grep 'esmini BUILD VERSION' "$WORK/last.out" "esmini-dyn identifies itself"
need_grep '^Usage:.*\[options\]' "$WORK/last.out" "esmini-dyn usage line"
