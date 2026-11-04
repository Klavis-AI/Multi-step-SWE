#!/usr/bin/env bash
# Requirement 17: the three changes this release makes to the esmini player's
# argument surface.
#
#   - `--disable_pline_interpolation` is REMOVED. It is no longer a known
#     argument, so it lands in the unrecognized-argument path: the player
#     prints `Unrecognized arguments:` followed by the offending argument,
#     prints the usage block, and exits non-zero WITHOUT running the scenario.
#   - `--window` and `--headless` now resolve in command-line order, last one
#     wins, so a `--window` before a `--headless` is cancelled by it.
#   - `--save_xosc` gains an optional `quit`/`continue` mode, defaulting to
#     `continue` when the value is omitted. Both modes write the resolved
#     scenario out; `quit` then exits before the scenario runs, so a `--record`
#     on the same command line produces no `.dat`, while `continue` runs it and
#     does produce one.
#
# Measured at both ends. At v2.44.2 `--disable_pline_interpolation` is accepted
# and the run exits 0, `--save_xosc quit` still writes the recording because the
# mode argument does not exist yet, and a `--window` before a `--headless` still
# wins - so all three parts below are real discriminators rather than assertions
# that happen to hold.
#
# esmini writes its outputs relative to the current directory, and every case
# runs with the working directory set to its own scratch $WORK, so nothing here
# can touch the fixture corpus.
source /tests/caselib.sh

##################### --disable_pline_interpolation is gone ###################
# --record is given alongside the rejected argument on purpose: see the .dat
# assertion below.
run_esmini --osc "$FIXTURES/cruise_straight.xosc" --headless \
           --fixed_timestep 0.1 --disable_pline_interpolation --record rejected.dat
rc=$?
[ "$rc" -ne 0 ] || fail "--disable_pline_interpolation was accepted (exit 0); it is removed at this release"
need_grep '^Unrecognized arguments:' "$WORK/last.out" "the removed option lands in the unrecognized-argument path"
need_grep '\-\-disable_pline_interpolation' "$WORK/last.out" "the unrecognized-argument report names the offending argument"
need_grep '^Usage:.*\[options\]' "$WORK/last.out" "the usage block is printed with the rejection"
# Rejecting the argument means not running the scenario at all. These two
# checks read the player's own log wording, which no document fixes, so they can
# only ever produce a false PASS - they are kept because they localise a failure
# cheaply, but the obligation does not rest on them.
need_not_grep 'Init Ego' "$WORK/last.out" "the scenario storyboard was not run after the argument was rejected"
need_not_grep 'runningState' "$WORK/last.out" "no storyboard element started after the argument was rejected"

# What the obligation rests on is the RECORDING, which depends on no wording at
# all. --record was given on the same command line above. The player still
# CREATES the file - the recorder is opened while the command line is being read
# - so the observation is its content, read back through dat2csv in the format
# requirement 15 fixes: a `Version:` preamble line, the `time, id, name, ...`
# column header, and then one row per recorded frame, each beginning with that
# frame's timestamp. A scenario that never ran contributes no such row.
# Measured across the step: at v2.44.2, where --disable_pline_interpolation is
# still accepted and the run proceeds, this same command records 302 frames; at
# v2.46.4 it records 0.
need_file "$WORK/rejected.dat" "the rejected run's recording file"
run_tool "$DAT2CSV" "$WORK/rejected.dat"
need_status 0 $? "dat2csv on the rejected run's recording"
need_file "$WORK/rejected.csv" "the converted recording"
frames=$(grep -c '^[0-9]' "$WORK/rejected.csv" || true)
[ "$frames" = 0 ] \
    || fail "the rejected run recorded $frames frames; requirement 17 says the scenario must not run at all"

########################## --save_xosc quit: no .dat ##########################
mkdir -p "$WORK/quit"
( cd "$WORK/quit" && WORK=$WORK/quit run_esmini --osc "$FIXTURES/cruise_straight.xosc" --headless \
      --fixed_timestep 0.1 --save_xosc quit --record rec.dat )
need_status 0 $? "--save_xosc quit"
[ -s "$WORK/quit/cruise_straight_0_of_0.xosc" ] \
    || fail "--save_xosc quit did not write the resolved scenario"
[ -e "$WORK/quit/rec.dat" ] \
    && fail "--save_xosc quit produced a recording; quit must exit before the scenario runs"

###################### --save_xosc default mode: continue #####################
# The value omitted entirely must behave as `continue`: the file is written AND
# the scenario runs to completion, so the recording appears.
mkdir -p "$WORK/cont"
( cd "$WORK/cont" && WORK=$WORK/cont run_esmini --osc "$FIXTURES/cruise_straight.xosc" --headless \
      --fixed_timestep 0.1 --save_xosc --record rec.dat )
need_status 0 $? "--save_xosc with the mode omitted"
[ -s "$WORK/cont/cruise_straight_0_of_0.xosc" ] \
    || fail "--save_xosc without a mode did not write the resolved scenario"
[ -s "$WORK/cont/rec.dat" ] \
    || fail "--save_xosc defaulted to quit; with the mode omitted it must default to continue and run the scenario"

# The written scenario is a real OpenSCENARIO document, not an empty placeholder.
need_grep '<OpenSCENARIO' "$WORK/cont/cruise_straight_0_of_0.xosc" "the saved scenario is an OpenSCENARIO document"

###################### --save_xosc continue: explicit keyword #################
# The explicit `continue` keyword must behave identically to the omitted form:
# both the resolved scenario file and the recording must appear.
mkdir -p "$WORK/explicit_cont"
( cd "$WORK/explicit_cont" && WORK=$WORK/explicit_cont run_esmini --osc "$FIXTURES/cruise_straight.xosc" --headless \
      --fixed_timestep 0.1 --save_xosc continue --record rec.dat )
need_status 0 $? "--save_xosc continue (explicit)"
[ -s "$WORK/explicit_cont/cruise_straight_0_of_0.xosc" ] \
    || fail "--save_xosc continue (explicit) did not write the resolved scenario"
[ -s "$WORK/explicit_cont/rec.dat" ] \
    || fail "--save_xosc continue (explicit) did not produce a recording; continue must run the scenario"
need_grep '<OpenSCENARIO' "$WORK/explicit_cont/cruise_straight_0_of_0.xosc" "the explicitly-continued saved scenario is an OpenSCENARIO document"

##################### --window / --headless resolve in order ##################
# The build profile fixes USE_OSG=False, so neither ordering can actually open a
# window and the run's own output is otherwise identical. What differs is
# whether the player ends up REQUESTING one, which it reports as a [warn] line.
# Measured at both ends, on this fixture, with the two orderings:
#
#            argument order                v2.44.2   v2.46.4
#   --window ... --headless                 warn      no warn
#   --headless --window ...                 warn      warn
#
# so the first run below is the discriminator - at v2.44.2 the trailing
# --headless does not cancel the earlier --window and the warning is still
# emitted - and the second is its control, pinning that the option is parsed and
# honoured at all rather than silently dropped. A candidate that simply stopped
# registering --window would fail the second.
mkdir -p "$WORK/wh" "$WORK/hw"
( cd "$WORK/wh" && WORK=$WORK/wh run_esmini --osc "$FIXTURES/cruise_straight.xosc" \
      --fixed_timestep 0.1 --window 60 60 800 400 --headless )
need_status 0 $? "--window before --headless"
need_not_grep 'window requested, but esmini compiled without OSG capabilities' \
              "$WORK/wh/last.out" "a --headless after --window cancels the window request"

( cd "$WORK/hw" && WORK=$WORK/hw run_esmini --osc "$FIXTURES/cruise_straight.xosc" \
      --fixed_timestep 0.1 --headless --window 60 60 800 400 )
need_status 0 $? "--window after --headless"
need_grep 'window requested, but esmini compiled without OSG capabilities' \
          "$WORK/hw/last.out" "a --window after --headless still requests the window"

exit 0
