#!/usr/bin/env bash
# Gate (proof of life). dat2csv must convert a .dat recording produced by `esmini
# --record` into a CSV file with a stable header and one row per frame.
source /tests/caselib.sh

cp "$FIXTURES/elev_cruise.xosc" "$WORK/elev_cruise.xosc"
cp "$FIXTURES/curves_elevation.xodr" "$WORK/curves_elevation.xodr"

run_esmini --osc elev_cruise.xosc --headless --fixed_timestep 0.05 --disable_log --record rec.dat
need_status 0 $? "esmini --record"
need_file "$WORK/rec.dat" "dat recording"

run_tool "$DAT2CSV" rec.dat
need_status 0 $? "dat2csv"
need_file "$WORK/rec.csv" "dat2csv output"

sed -n '2p' "$WORK/rec.csv" > "$WORK/hdr"
need_grep '^time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot$' "$WORK/hdr" "csv header"

# R05.43: the recording must be at format version 2, and dat2csv carries that
# version through into the file it writes -- `Version: %d, OpenDRIVE: ..., 3DModel: ...`.
# Only "version 2" is asserted: the row names version 2 as the format this step
# keeps writing, and the OpenDRIVE/3DModel tails are the recording's own
# filenames, which the candidate's build directory decides. The .dat is binary,
# so the version is read where it is textual -- the converter's first line.
sed -n '1p' "$WORK/rec.csv" > "$WORK/ver"
need_grep '^Version: 2,' "$WORK/ver" "the recording is at .dat format version 2"

sed -n '3p' "$WORK/rec.csv" > "$WORK/first"
need_grep '^0\.000, 0, Ego, 100\.114, 1\.399, -2\.473,' "$WORK/first" "first frame position"
need_grep ', 10\.000, 0\.000, 0\.000$' "$WORK/first" "first frame speed and wheel state"

# R05.44: `dat2csv` run with no arguments still prints its `Usage:` line.
# instruction.md:222 -- the closing scope paragraph, "dat2csv run with no
# arguments still prints its Usage: line" -- names that obligation explicitly,
# so it is asserted here.
# Only the `Usage: ` prefix is required -- the rest of the line names the
# tool's own path at run time, which is a property of where the candidate
# built it, not of their solution. The non-zero exit is the documented
# rejection; the exact code is not.
run_tool "$DAT2CSV"
rc=$?
[ "$rc" -ne 0 ] || fail "dat2csv with no arguments must not report success (exit $rc)"
need_grep '^Usage: ' "$WORK/last.out" "dat2csv prints its Usage: line with no arguments"
exit 0
