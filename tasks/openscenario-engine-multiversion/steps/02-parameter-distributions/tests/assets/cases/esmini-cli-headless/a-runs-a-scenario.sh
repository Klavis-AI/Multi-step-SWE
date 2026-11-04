#!/usr/bin/env bash
# Gate (proof of life). The engine must still run a scenario end to end with no window,
# on a fixed timestep, with controllers disabled. This is a smoke test of the
# command-line entry point, not of scenario semantics.
#
# Two things in this output must NOT be asserted, both measured at v2.23.1 and
# v2.27.0:
#   * "Generated seed <n>" is a fresh random number on every run, so no
#     whole-file comparison and no line-number-based assertion is valid here;
#   * the "Gateway did not provide state for external car" lines and the
#     Maneuver start/end transitions both CHANGE inside this step's range.
# Only behaviour that is invariant across the step is checked.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$WORK/two_car_brake.xosc"
cp "$FIXTURES/straight_500m.xodr" "$WORK/straight_500m.xodr"

# The scenario is named relative to $WORK. esmini echoes back the path it was
# handed, so passing an absolute path would put this run's unique scratch
# directory into the output and make any assertion on it machine-dependent.
run_esmini --osc two_car_brake.xosc --headless --fixed_timestep 0.05 --disable_controllers \
    --csv_logger run.csv --record trace.dat
need_status 0 $? "esmini headless run of two_car_brake.xosc"

need_grep '^Loading two_car_brake\.xosc \(v1\.0\)' "$WORK/last.out" \
    "the scenario was loaded and its OpenSCENARIO version reported"

# Both entities must be initialised. These Init transitions are the part of the
# storyboard trace that is stable across this step.
for ent in Ego Target; do
    need_grep "^0\.000: Init $ent TeleportAction standbyState -> startTransition -> runningState" \
        "$WORK/last.out" "$ent is teleported into place at t=0"
    need_grep "^0\.000: Init $ent TeleportAction runningState -> endTransition -> completeState" \
        "$WORK/last.out" "$ent's teleport completes at t=0"
    need_grep "^0\.000: Init $ent LongitudinalAction standbyState -> startTransition -> runningState" \
        "$WORK/last.out" "$ent's initial speed action starts at t=0"
done

# --headless means no window was opened; a run that fell back to a viewer would
# say so here.
need_not_grep 'osgViewer|Creating window|GraphicsWindow' "$WORK/last.out" \
    "no graphics window was created"

# The --csv_logger column header is an inherited format obligation: both
# instruction.md ("Exact output that must not change") and the handoff state
# that it is byte-identical across this step, and it is, at v2.23.1 and v2.27.0
# alike. Only the leading prefix is matched -- the per-entity block repeats
# once per entity and its width is a property of the scenario, not of the
# format. `Entitity` is upstream's misspelling and is deliberately reproduced;
# a solution that "fixes" it has changed the format.
need_file "$WORK/run.csv" "the --csv_logger recording"

# Six banner lines precede the header, in this order. The values after the four
# version colons depend on how the tree was built, so only the prefixes are
# matched; the last two lines carry real content and are matched in full. The
# scenario name is echoed back exactly as it was given on the command line,
# which is why the run above names it relative to $WORK.
need_grep '^esmini GIT REV: '        "$WORK/run.csv" "the csv_logger banner opens with the git revision line"
need_grep '^esmini GIT TAG: '        "$WORK/run.csv" "the csv_logger banner carries the git tag line"
need_grep '^esmini GIT BRANCH: '     "$WORK/run.csv" "the csv_logger banner carries the git branch line"
need_grep '^esmini BUILD VERSION: '  "$WORK/run.csv" "the csv_logger banner carries the build version line"
need_grep '^Scenario File Name: two_car_brake\.xosc$' "$WORK/run.csv" \
    "the csv_logger banner names the scenario as it was given on the command line"
need_grep '^Number of Vehicles: 2$' "$WORK/run.csv" \
    "the csv_logger banner counts the scenario's two entities"

need_grep '^Index \[-\] , TimeStamp \[s\] , #1 Entitity_Name \[-\] , #1 Entitity_ID \[-\] , #1 Current_Speed \[m/s\] , ' \
    "$WORK/run.csv" "the --csv_logger column header is unchanged"

# The recording/decoder pair is the other inherited output obligation of this
# entry point, and it is checked here rather than only in a scored group so a
# regression in it vetoes the step instead of costing one share. dat2csv
# derives its output name from its input and writes it beside the input; the
# `Version:` line is the decoded file's first line and the column header its
# second, so the header is deliberately not matched as line 1.
need_file "$WORK/trace.dat" "the --record recording"
run_tool "$DAT2CSV" "$WORK/trace.dat"
need_status 0 $? "dat2csv decoding the recording this run produced"
need_file "$WORK/trace.csv" "dat2csv's output, named after its input and written beside it"
need_grep '^Version: 2, OpenDRIVE: straight_500m\.xodr, 3DModel: ' \
    "$WORK/trace.csv" "the decoded recording names its format version and road network"
need_grep '^time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot$' \
    "$WORK/trace.csv" "the decoded recording carries the documented column header"
need_grep '^0\.000, 0, Ego, ' "$WORK/trace.csv" \
    "the first sample is the first-declared entity, at id 0, under its scenario name"
