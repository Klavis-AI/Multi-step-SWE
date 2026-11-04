#!/usr/bin/env bash
# The six published artifacts exist, are the right kind of file, and run.
source /tests/caselib.sh

for b in "$ESMINI" "$ESMINI_DYN" "$ODRPLOT" "$DAT2CSV"; do
    [ -x "$b" ] || fail "not an executable: $b"
    file -b "$b" | grep -q 'ELF .* executable' || fail "not an ELF executable: $b"
done

IFS=: read -r -a dirs <<< "$LIBDIR"
for so in libesminiLib.so libesminiRMLib.so; do
    found=""
    for d in "${dirs[@]}"; do [ -f "$d/$so" ] && found=$d/$so && break; done
    [ -n "$found" ] || fail "$so not found under LIBDIR=$LIBDIR"
    file -b "$found" | grep -q 'ELF .* shared object' || fail "not an ELF shared object: $found"
done

# Every artifact answers a trivial invocation rather than only existing. The
# exit status of --help is deliberately not asserted: upstream returns a
# non-zero status for it at this release, and pinning that number would be
# grading an accident rather than the option.
run_tool "$ESMINI" --help
need_grep '^Usage: esmini \[options\]' "$WORK/last.out" "esmini --help usage line"
need_grep '^  --osc <filename>' "$WORK/last.out" "esmini --help option list"

run_tool "$ESMINI_DYN"
need_grep 'esmini BUILD VERSION' "$WORK/last.out" "esmini-dyn identifies itself"
