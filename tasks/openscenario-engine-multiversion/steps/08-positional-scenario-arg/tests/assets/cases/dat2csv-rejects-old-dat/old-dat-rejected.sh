#!/usr/bin/env bash
source /tests/caselib.sh

# The .dat recording format changed in this release. A recording written by the
# starting release must be recognised as older and refused, not silently
# mis-parsed. The fixture is a real v2 recording: the version as one leading
# word (`2`), then the v2 header - the OpenDRIVE and model file names in
# fixed-size character arrays - then one fixed-size binary record per entity
# per frame.
#
# The recording is copied into $WORK before it is read. dat2csv derives its
# output path from its INPUT path and creates that output file before it
# validates the version, so running it directly on $FIXTURES/old_v2/sim.dat
# would drop a stray sim.csv into the verifier's own fixture corpus. Cases do
# not write to $FIXTURES.

old="$WORK/sim.dat"
cp "$FIXTURES/old_v2/sim.dat" "$old"
need_file "$old"

run_tool "$DAT2CSV" --file "$old"
need_status 255 $? "old recording exit status"
need_grep 'Incompatible DAT major file version' "$WORK/last.out" "old version reported"

# Nothing must be produced: a partial CSV from a mis-parsed recording is worse
# than no CSV at all. The tool may create its output file before it reads the
# header, so what is required is that no rows reach it, not that it is absent.
need_not_grep '^time,' "$WORK/last.out" "no csv rows emitted"
[ -s "$WORK/sim.csv" ] && fail "dat2csv wrote a non-empty CSV for a rejected recording"

# A file that is not a recording at all is refused the same way.
printf 'this is not a recording\n' > "$WORK/garbage.dat"
run_tool "$DAT2CSV" --file "$WORK/garbage.dat"
need_status 255 $? "garbage exit status"
need_grep 'Incompatible DAT major file version' "$WORK/last.out" "garbage reported as wrong version"
[ -s "$WORK/garbage.csv" ] && fail "dat2csv wrote a non-empty CSV for a garbage input"

# And a missing file is still told apart from a wrong-version one.
run_tool "$DAT2CSV" --file "$WORK/nope.dat"
need_status 255 $? "missing file exit status"
need_grep 'Cannot open file' "$WORK/last.out" "missing file reported"
exit 0
