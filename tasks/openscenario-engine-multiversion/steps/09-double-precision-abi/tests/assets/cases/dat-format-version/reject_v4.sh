#!/usr/bin/env bash
# A format-4 recording written by v2.59.0 must be rejected by the v3.0.4
# dat2csv. The major-version bump is not forward-compatible: the new tool
# refuses the file rather than silently decoding it as v5.
#
# The recording is copied into $WORK first: dat2csv derives its output path from
# its input path, so reading the fixture in place would leave a stray .csv in
# the verifier's fixture corpus.
source /tests/caselib.sh

rec="$WORK/recording.dat"
cp "$FIXTURES/recording_format4.dat" "$rec"
need_file "$rec"

run_tool "$DAT2CSV" "$rec"
[ "$?" -eq 0 ] && fail "dat2csv accepted a format-4 recording"
need_grep 'Incompatible DAT major file version' "$WORK/last.out" "version error message"

# The offending version must be named. Anchored to the message itself: a bare
# match for "4" would also be satisfied by the input path or the scratch
# directory name, which would make this assertion vacuous.
need_grep 'Incompatible DAT major file version:? *4' "$WORK/last.out" "offending version reported"

# A rejected recording must not yield rows. dat2csv derives <basename>.csv from
# the input path and creates it before it reads the header, so the requirement
# is that it is left empty - no rows, and not even a column header.
#
# The file is required to be present and zero bytes rather than merely "not
# non-empty". Tolerance of an absent file would let the whole assertion be
# satisfied by a tool that wrote nothing because it never got as far as the
# output path, and requirement 7 asks for an empty derived CSV, not for no
# derived CSV. Both halves are asserted below so neither can stand in for the
# other: the file exists, and it is empty.
csv="$WORK/recording.csv"
[ -f "$csv" ]  || fail "dat2csv left no derived CSV for a rejected recording"
[ ! -s "$csv" ] || fail "dat2csv left a non-empty CSV for a rejected recording ($(wc -c < "$csv") bytes)"
exit 0
