#!/usr/bin/env bash
source /tests/caselib.sh

# The six published artifacts exist, are the right kind of file, and run.
#
# The --osc usage line is deliberately matched with an alternation: at the
# starting release it reads "--osc <filename>", at the target release it
# reads "[--osc] <filename>" because --osc becomes a default argument (see
# the ess-osc-positional group for the assertion that distinguishes the two).
# A gate obligation must hold at both ends of the step, so this check
# does not pin either exact spelling.

for b in "$ESMINI" "$ESMINI_DYN" "$ODRPLOT" "$DAT2CSV"; do
    [ -x "$b" ] || fail "not an executable: $b"
    file -b "$b" | grep -q 'ELF .* executable' || fail "not an ELF executable: $b"
done

# LIBDIR is documented as "the" directory holding both shared libraries, but
# TEST_INTERFACE.md only promises each of the six artifacts exists somewhere
# under the built tree ("the build tree is also searched"), not that the two
# libraries share a directory. Check LIBDIR first and fall back to a search
# rooted at the tree that RESOURCES already anchors us to.
treeroot=$(dirname "$RESOURCES")
IFS=: read -r -a dirs <<< "$LIBDIR"
for so in libesminiLib.so libesminiRMLib.so; do
    found=""
    for d in "${dirs[@]}"; do [ -f "$d/$so" ] && found=$d/$so && break; done
    [ -n "$found" ] || found=$(find "$treeroot" -name "$so" -not -path '*/externals/*' 2>/dev/null | head -1)
    [ -n "$found" ] || fail "$so not found under LIBDIR=$LIBDIR or under $treeroot"
    file -b "$found" | grep -q 'ELF .* shared object' || fail "not an ELF shared object: $found"
done

run_tool "$ESMINI" --help
need_grep '^Usage: esmini \[options\]' "$WORK/last.out" "esmini --help usage line"
need_grep '(\[--osc\]|--osc) <filename>' "$WORK/last.out" "esmini --help option list"

run_tool "$ESMINI_DYN"
need_grep 'esmini BUILD VERSION' "$WORK/last.out" "esmini-dyn identifies itself"
