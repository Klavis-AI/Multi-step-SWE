#!/usr/bin/env bash
# INHERITED. WORKSPACE_HANDOFF.md's "What you receive" says the tree you start
# from "records playback to `.dat` files" and "parses those recordings through
# `dat2csv`". That round trip predates this step and must survive it. Every
# value below was read from real binaries at BOTH v2.27.5 and v2.31.10.
#
# The two ends differ by exactly one thing -- v2.31.10 writes the final sample
# twice, so the converted CSV is 408 lines against v2.27.5's 406. Nothing here
# asserts a row count for that reason; the Version line, the header row and
# every sample this case names are byte-identical at both tags.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_esmini --osc "$WORK/two_car_brake.xosc" --headless --fixed_timestep 0.05 \
           --record "$WORK/out.dat"
need_status 0 $? "esmini headless run with --record"
need_file "$WORK/out.dat"

# dat2csv writes its result into the CURRENT WORKING DIRECTORY under the
# recording's basename with the suffix replaced -- not beside the input -- and
# says nothing on stdout when it succeeds. The harness runs every case with
# the working directory set to $WORK, but make that explicit rather than rely
# on it.
( cd "$WORK" && run_tool "$DAT2CSV" "$WORK/out.dat" )
need_status 0 $? "dat2csv on the recording esmini just made"
need_file "$WORK/out.csv"

need_grep '^Version: 2, OpenDRIVE: straight_500m\.xodr, 3DModel: *$' "$WORK/out.csv" \
    "the converted recording opens with its own version and network line"
need_grep '^time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot$' "$WORK/out.csv" \
    "the per-sample column header is unchanged"

# Both entities are in the recording, and the first sample of each agrees with
# what SE_GetObjectState reports for the same scenario at t=0.
need_grep '^0\.000, 0, Ego, 20\.000, -1\.535, 0\.000, 0\.000, 0\.000, 0\.000, 20\.000,' "$WORK/out.csv" \
    "Ego's first recorded sample"
need_grep '^0\.000, 1, Target, 120\.000, -1\.535, 0\.000, 0\.000, 0\.000, 0\.000, 10\.000,' "$WORK/out.csv" \
    "Target's first recorded sample"

# The recording runs to the storyboard's own stop trigger, by which point Ego
# has braked from 20 m/s to 5 m/s.
need_grep '^10\.050, 0, Ego, 153\.125, -1\.535, 0\.000, 0\.000, 0\.000, 0\.000, 5\.000,' "$WORK/out.csv" \
    "Ego's last recorded sample, after the brake manoeuvre"

# Sanity on the shape rather than the length: every data row carries the same
# twelve fields the header names.
ncol=$(awk -F, 'NR==2 {print NF}' "$WORK/out.csv")
[ "$ncol" -eq 12 ] || fail "the converted recording's header names $ncol fields, not 12"
bad=$(awk -F, 'NR>2 && NF != 12 {c++} END {print c+0}' "$WORK/out.csv")
[ "$bad" -eq 0 ] || fail "$bad sample rows do not carry 12 fields"
