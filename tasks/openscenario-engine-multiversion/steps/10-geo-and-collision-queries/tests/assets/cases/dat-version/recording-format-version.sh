#!/usr/bin/env bash
# The .dat recording format carries its own version, independent of the release
# number. esmini --record writes it and dat2csv --file_refs prints it as the
# first line of the decoded CSV, ahead of the column header.
#
# Across this step the recorded layout changes and the version moves with it:
# a start-tag build writes 5.1 here. The exact string is asserted, because a
# format that changed without moving its version number is precisely the defect
# a version field exists to prevent.
#
# The second half of that contract is the other direction: the MAJOR version is
# what decides compatibility, so moving the minor must not narrow what the
# decoder accepts. That half is checked below against a recording stamped 5.1
# rather than 5.3 — the stamp itself comes off the previous release's own
# recording, which the corpus ships.
#
# The third part is what the corpus can and cannot ask about a recording this
# tree did not write. Refusing one is decided by its FIRST WORD, and that word
# is the one part of a recording's layout any document fixes ("its header
# begins with two unsigned words, a major version followed by a minor one",
# step 08 requirement 1) — so a recording from another implementation can be
# handed to the decoder and told to be refused, which the last section does
# with a real format-4 recording. Decoding one is not the same question: past
# the version words the layout belongs to the writer and its reader jointly
# ("Beyond the two version words the layout is yours"), so the only recording
# the whole contents of which this case may read is one the candidate's own
# writer produced. That is why the 5.1 half below is built out of `rec.dat`.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_esmini --osc "$WORK/two_car_brake.xosc" --headless --fixed_timestep 0.05 \
           --record "$WORK/rec.dat"
need_status 0 $? "esmini with --record"
need_file "$WORK/rec.dat"

run_tool "$DAT2CSV" --file "$WORK/rec.dat" --file_refs
need_status 0 $? "dat2csv --file_refs"
need_file "$WORK/rec.csv"

# The version line is the first line of the file, not merely somewhere in it.
head -1 "$WORK/rec.csv" > "$WORK/first.line"
need_grep '^Version: 5\.3,' "$WORK/first.line" "recording format version"
need_grep 'OpenDRIVE: .*straight_500m\.xodr' "$WORK/first.line" "ODR reference in the preamble"

# All three documented fields, in the documented order: the preamble names the
# OpenDRIVE file and the 3D model file, each behind its own label. The label
# and the comma after it are asserted because requirement 6b states them; the
# model path itself is deliberately NOT pinned, because this run loads the
# scenario's own model names and no document promises what the recorder stores
# for them. The value here is empty, so the label is followed by nothing and
# then the field that comes after it.
need_grep '^Version: [0-9.]+, OpenDRIVE: [^,]+, 3DModel: ' "$WORK/first.line" \
    "preamble carries all three documented fields in order"

# The preamble is an addition, not a replacement: the usual column header and
# the usual rows follow it.
need_grep '^time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot$' \
    "$WORK/rec.csv" "column header follows the preamble"
need_grep '^0\.000, 0, Ego, 20\.000, -1\.535, 0\.000, 0\.000, 0\.000, 0\.000, 20\.000, ' \
    "$WORK/rec.csv" "first Ego sample"
need_grep '^0\.000, 1, Target, 120\.000, -1\.535, ' "$WORK/rec.csv" "first Target sample"
rows=$(grep -c '^[0-9]' "$WORK/rec.csv")
[ "$rows" -ge 400 ] && [ "$rows" -le 410 ] || fail "expected about 404 sample rows, got $rows"

# A recording the candidate's own engine wrote must be readable by the
# candidate's own decoder. That is asserted positively, by what the decode
# produced rather than by what it did not print: it exited 0 above, it emitted
# the whole row band above, and it reached the last sample of the run rather
# than stopping part-way. A decoder that rejected the version would satisfy
# none of those.
# The scenario stops on simulation time greaterThan 10, so the recording runs
# into its eleventh second for both entities. Matched on the second rather than
# on the exact final step boundary, which is the engine's business.
need_grep '^10\.[0-9]+, 0, Ego, ' "$WORK/rec.csv" "decode reaches the last Ego sample"
need_grep '^10\.[0-9]+, 1, Target, ' "$WORK/rec.csv" "decode reaches the last Target sample"

# Cross-release playback: moving the minor version must not narrow what the
# decoder accepts. Compatibility is decided by the MAJOR version alone, so a
# recording stamped 5.1 has to decode exactly as the 5.3 one just written does.
#
# The older recording is built here out of the candidate's OWN, by taking the
# version words from the previous release's own recording (`two_car_brake_v3.0.4.dat`,
# shipped in the corpus) and writing them over the candidate's: those two words
# are the whole of a recording's layout that any document fixes, so they are the
# only part of a foreign recording this case may read, and everything from the
# ninth byte on stays the candidate's own writer's work. The stamp is therefore
# the real previous release's - asserted to be major 5, minor 1 below - rather
# than a number typed into this script. The version the decoder must REPORT
# changes with the word: it reports what the file carries, 5.1, not what the
# engine now writes.
cp "$WORK/rec.dat" "$WORK/old.dat"
python3 - "$WORK/old.dat" "$FIXTURES/two_car_brake_v3.0.4.dat" <<'PY' || fail "could not stamp the recording with the previous release's version words"
import struct, sys
own, previous = sys.argv[1], sys.argv[2]
with open(previous, "r+b") as f:
    them = struct.unpack("<II", f.read(8))
if them != (5, 1):
    sys.exit("the shipped previous-release recording reads %d.%d, not 5.1" % them)
with open(own, "r+b") as f:
    major, minor = struct.unpack("<II", f.read(8))
    if major != 5:
        sys.exit("this tree's own recording reads major %d, not the supported version 5" % major)
    f.seek(0)
    f.write(struct.pack("<II", them[0], them[1]))
PY
run_tool "$DAT2CSV" --file "$WORK/old.dat" --file_refs
need_status 0 $? "dat2csv on a recording stamped 5.1"
need_file "$WORK/old.csv"

head -1 "$WORK/old.csv" > "$WORK/old.first.line"
need_grep '^Version: 5\.1,' "$WORK/old.first.line" \
    "the older recording is reported with its own format version, not this one"
need_grep '^Version: [0-9]+\.[0-9]+, OpenDRIVE: [^,]+, 3DModel: ' "$WORK/old.first.line" \
    "the older recording's preamble carries the same three fields in the same order"

# "Accepted" means decoded, not merely opened without an error: the same column
# header, real sample rows, and a decode that runs to the end of the recording
# rather than stopping at a version check.
need_grep '^time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot$' \
    "$WORK/old.csv" "column header of the older recording"
need_grep '^0\.000, 0, Ego, 20\.000, -1\.535, ' "$WORK/old.csv" \
    "first Ego sample of the older recording"
oldrows=$(grep -c '^[0-9]' "$WORK/old.csv")
[ "$oldrows" -ge 400 ] && [ "$oldrows" -le 410 ] \
    || fail "expected about 404 sample rows from the older recording, got $oldrows"
need_grep '^10\.[0-9]+, 1, Target, ' "$WORK/old.csv" \
    "decode of the older recording reaches its last sample"

# A recording from OUTSIDE this tree's writer-reader pair. Compatibility is
# decided by the major version alone, and this file's major is 4 - the format
# of the release before the last one - so the decoder must refuse it whatever
# the rest of it holds, which is what makes a foreign recording usable at all
# here: the refusal is decided by the first word, the one piece of the layout
# every document fixes, so nothing after it has to be understood for this
# check to be fair. `recording_format4.dat` is that release's own recording of
# a two-entity braking scenario - entities `Ego` and `Target`, models
# `car_white.osgb` and `car_red.osgb`, one `BrakeEvent` - shipped as it was
# written.
#
# The message and the exit status are the ones requirement 5/7 of the steps
# that introduced them fix: the words `Incompatible DAT major file version`,
# the version found immediately after them, and a non-zero exit. The derived
# CSV is created and left empty - no rows, not even a column header - because
# a decoder that emits a partial table from a file it does not understand is
# the failure the version field exists to prevent.
cp "$FIXTURES/recording_format4.dat" "$WORK/older.dat"
run_tool "$DAT2CSV" --file "$WORK/older.dat"
[ "$?" -eq 0 ] && fail "dat2csv accepted a format-4 recording"
need_grep 'Incompatible DAT major file version' "$WORK/last.out" \
    "a foreign older-major recording is refused by name"
need_grep 'Incompatible DAT major file version:? *4' "$WORK/last.out" \
    "the refused recording's own version is named"
older_csv="$WORK/older.csv"
[ -f "$older_csv" ]  || fail "dat2csv left no derived CSV for the refused recording"
[ ! -s "$older_csv" ] || fail "dat2csv left a non-empty CSV for the refused recording ($(wc -c < "$older_csv") bytes)"
