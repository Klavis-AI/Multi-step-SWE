#!/usr/bin/env bash
# Gate (proof of life). The two conversion/plot tools are published artifacts, and their
# surfaces are inherited: every string, path and number asserted here was
# measured identical at v2.46.4 and at v2.51.1, so a solution that does not
# disturb them passes without doing anything.
#
# This matters beyond this step. Step 08 rewrites dat2csv and bumps the `.dat`
# container to major version 4, and it can only express "a v2.51.1 recording is
# rejected" if v2.51.1 really did write version 2. That fact is asserted here
# rather than assumed there.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$WORK/two_car_brake.xosc"
cp "$FIXTURES/straight_500m.xodr" "$WORK/straight_500m.xodr"
mkdir -p "$WORK/rec"

# --- dat2csv --------------------------------------------------------------
# No arguments at all: the usage line, and a non-zero status. The banner
# embeds argv[0], so only its shape is matched, never the path it was run from.
run_tool "$DAT2CSV"
rc=$?
[ "$rc" -ne 0 ] || fail "dat2csv with no arguments exited 0"
need_grep 'Usage:.*dat2csv <filename>' "$WORK/last.out" "dat2csv usage line"

# Record a run, then convert it. The recording is written into a SUBDIRECTORY
# and dat2csv is invoked from $WORK, because where the CSV lands is part of the
# surface: at this tag the output is named from the basename of the input and
# written into the CURRENT WORKING DIRECTORY, not beside the .dat. Step 08
# moves it beside the .dat, so a case that ran dat2csv from the recording's own
# directory could not tell the two behaviours apart.
run_esmini --osc two_car_brake.xosc --headless --fixed_timestep 0.05 --record rec/out.dat
need_status 0 $? "esmini with --record"
need_file "$WORK/rec/out.dat" "the recording"

run_tool "$DAT2CSV" rec/out.dat
need_status 0 $? "dat2csv on a fresh recording"
need_grep 'dat version: 2' "$WORK/last.out" "the .dat container is still format version 2"
[ -f "$WORK/rec/out.csv" ] && fail "dat2csv wrote the CSV beside the .dat instead of into the working directory"
need_file "$WORK/out.csv" "dat2csv output in the working directory"

# The version line and the plain header, both unchanged at this tag.
sed -n '1p' "$WORK/out.csv" > "$WORK/v"
sed -n '2p' "$WORK/out.csv" > "$WORK/h"
need_grep '^Version: 2, OpenDRIVE: straight_500m\.xodr, 3DModel: ' "$WORK/v" "dat2csv version line"
need_grep '^time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot$' "$WORK/h" "dat2csv plain header"

# --- a recording written by a much older release --------------------------
# The container format has been version 2 for the whole ladder up to here, so a
# recording produced by v2.20.10 must still convert. The expected rows are the
# same two-car scenario, so they can be pinned: a reader that mis-parsed the
# older layout would produce garbage coordinates rather than fail outright.
cp "$FIXTURES/two_car_brake_v2.20.10.dat" "$WORK/legacy.dat"
run_tool "$DAT2CSV" legacy.dat
need_status 0 $? "dat2csv on a v2.20.10 recording"
need_grep 'dat version: 2' "$WORK/last.out" "a v2.20.10 recording still reads as version 2"
need_file "$WORK/legacy.csv" "converted v2.20.10 recording"
n=$(wc -l < "$WORK/legacy.csv")
[ "$n" -eq 206 ] || fail "expected 206 lines from the v2.20.10 recording, got $n"
sed -n '3p' "$WORK/legacy.csv" > "$WORK/lf"
tail -1 "$WORK/legacy.csv" > "$WORK/ll"
need_grep '^0\.000, 0, Ego, 20\.000, -1\.535, 0\.000, 0\.000, 0\.000, 0\.000, 20\.000,' "$WORK/lf" "first row of the legacy recording"
need_grep '^10\.100, 1, Target, 221\.000, -1\.535, 0\.000, 0\.000, 0\.000, 0\.000, 10\.000,' "$WORK/ll" "last row of the legacy recording"

# --- odrplot ---------------------------------------------------------------
# odrplot samples the road geometry to a CSV named track.csv in the working
# directory. Its output for this fixture is byte-identical at both ends of this
# step; the row count and the two pinned rows are enough to catch a resampling
# or a units change without pinning all 3514 lines.
run_tool "$ODRPLOT" straight_500m.xodr
need_status 0 $? "odrplot"
need_file "$WORK/track.csv" "odrplot track.csv"
n=$(wc -l < "$WORK/track.csv")
[ "$n" -eq 3514 ] || fail "expected 3514 lines from odrplot, got $n"
sed -n '1p' "$WORK/track.csv" > "$WORK/t1"
sed -n '2p' "$WORK/track.csv" > "$WORK/t2"
need_grep '^lane, 1, 0, 3, no-driving$' "$WORK/t1" "odrplot lane header row"
need_grep '^0\.000000, 10\.750000, 0\.000000, 0\.000000$' "$WORK/t2" "odrplot first sample"
exit 0
