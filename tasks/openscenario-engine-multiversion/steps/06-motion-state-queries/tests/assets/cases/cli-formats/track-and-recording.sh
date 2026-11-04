#!/usr/bin/env bash
# Requirement 15: the three file formats the command-line tools emit -
# odrplot's track file, the .dat recording as converted by dat2csv, and the
# --csv_logger table - must come out of this step unchanged.
#
# Every constant below was measured at BOTH ends of the step and is identical
# at each, which is what makes it legitimate for a gate obligation. The
# esmini usage line is NOT among them: it reads "Usage: esmini [options]" at
# v2.44.2 and "Usage:  [options]" at v2.46.4, so it is matched tolerantly
# where it appears at all. Nor is the [info] volume near the end of the case:
# that obligation is the floor the logging requirement publishes, not a count
# taken off either end of the step, and the comment there says why.
source /tests/caselib.sh

############################# odrplot track file #############################
# odrplot writes its default output relative to the current directory, and
# every case runs with the working directory set to its own scratch $WORK, so
# this cannot touch the fixture corpus.
run_tool "$ODRPLOT" "$FIXTURES/straight_500m.xodr"
need_status 0 $? "odrplot on straight_500m.xodr"
need_grep 'Created track\.csv using stepsize 1\.00' "$WORK/last.out" "odrplot reports its output and stepsize"
need_file "$WORK/track.csv"

lanes=$(grep -c '^lane' "$WORK/track.csv")
[ "$lanes" = 7 ] || fail "expected 7 lane records in track.csv, got $lanes"
# Lane ids run 3 down to -3 with only 1 and -1 drivable, matching the two
# driving lanes the road manager reports for the same network.
for spec in "3 no-driving" "2 no-driving" "1 driving" "0 no-driving" \
            "-1 driving" "-2 no-driving" "-3 no-driving"; do
    set -- $spec
    n=$(grep -c "^lane, 1, 0, $1, $2\$" "$WORK/track.csv")
    [ "$n" = 1 ] || fail "expected exactly one 'lane, 1, 0, $1, $2' record, got $n"
done
total=$(wc -l < "$WORK/track.csv")
[ "$total" = 3514 ] || fail "expected 3514 lines in track.csv (7 lane records + 3507 samples), got $total"

########################### .dat recording -> csv ############################
# The recording is written into $WORK, not alongside the fixture: dat2csv
# derives its output path from its INPUT path, so converting a fixture in
# place would write into the graded corpus.
run_esmini --osc "$FIXTURES/cruise_straight.xosc" --headless \
           --fixed_timestep 0.05 --record "$WORK/sim.dat"
need_status 0 $? "esmini recording run"
need_file "$WORK/sim.dat"

run_tool "$DAT2CSV" "$WORK/sim.dat"
need_status 0 $? "dat2csv conversion"
need_file "$WORK/sim.csv"

need_grep '^Version: 2, OpenDRIVE: straight_500m\.xodr, 3DModel: *$' "$WORK/sim.csv" "dat2csv preamble"
head -2 "$WORK/sim.csv" | tail -1 > "$WORK/dat_header.csv"
need_grep '^time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot$' \
          "$WORK/dat_header.csv" "dat2csv column header"
rows=$(wc -l < "$WORK/sim.csv")
[ "$rows" = 604 ] || fail "expected 604 lines in sim.csv (preamble + header + 602 frames), got $rows"
need_grep '^0\.000, 0, Ego, 20\.000, -1\.535, 0\.000, 0\.000, 0\.000, 0\.000, 20\.000,' \
          "$WORK/sim.csv" "first recorded frame"
tail -1 "$WORK/sim.csv" > "$WORK/dat_last.csv"
# Ego runs the full 500 m road and is stopped at the end of the recording.
need_grep '^30\.050, 0, Ego, 500\.000, -1\.535, 0\.000, 0\.000, 0\.000, 0\.000, 0\.000,' \
          "$WORK/dat_last.csv" "last recorded frame"

############################### --csv_logger #################################
run_esmini --osc "$FIXTURES/cruise_straight.xosc" --headless \
           --fixed_timestep 0.05 --csv_logger "$WORK/log.csv"
need_status 0 $? "esmini --csv_logger run"
need_file "$WORK/log.csv"

# Six preamble lines, then one header line, then one line per frame.
loglines=$(wc -l < "$WORK/log.csv")
[ "$loglines" = 609 ] || fail "expected 609 lines in the csv_logger table (6 preamble + header + 602 frames), got $loglines"
sed -n '1p' "$WORK/log.csv" > "$WORK/p.csv"; need_grep '^esmini GIT REV: ' "$WORK/p.csv" "preamble line 1"
sed -n '4p' "$WORK/log.csv" > "$WORK/p.csv"; need_grep '^esmini BUILD VERSION: ' "$WORK/p.csv" "preamble line 4"
sed -n '5p' "$WORK/log.csv" > "$WORK/p.csv"; need_grep '^Scenario File Name: .*cruise_straight\.xosc$' "$WORK/p.csv" "preamble names the scenario"
sed -n '6p' "$WORK/log.csv" > "$WORK/p.csv"; need_grep '^Number of Vehicles: 1$' "$WORK/p.csv" "preamble counts the vehicles"

# csv_column reads the FIRST line of the file it is given as the header, so
# the header line is extracted on its own.
sed -n '7p' "$WORK/log.csv" > "$WORK/log_header.csv"
[ "$(csv_column 'Index [-]' "$WORK/log_header.csv")" = 1 ] || fail "Index is not the first column"
[ "$(csv_column 'TimeStamp [s]' "$WORK/log_header.csv")" = 2 ] || fail "TimeStamp is not the second column"
need_grep '#1 Entity_Name \[-\]' "$WORK/log_header.csv" "per-entity columns are present"
need_grep '#1 Current_Speed \[m/s\]' "$WORK/log_header.csv" "speed column is present"

# csv_column strips whitespace and quotes from each field before comparing, so
# the two checks above would pass through a change in the header's SPELLING.
# Requirement 15 says the csv_logger header format is unchanged, and the part
# of it that is easiest to "tidy" by accident is the separator: this release
# writes " , " - a space BEFORE each comma as well as after - and closes the
# line with a trailing " , " and nothing after it. Both ends of the step emit
# these 754 bytes identically; the first five fields and the last two are
# pinned literally so that respelling the separator is caught here.
need_grep '^Index \[-\] , TimeStamp \[s\] , #1 Entity_Name \[-\] , #1 Entity_ID \[-\] , #1 Current_Speed \[m/s\] ,' \
          "$WORK/log_header.csv" "the header's leading fields keep their exact spacing"
need_grep ', #1 Road_Curvature \[1/m\] , #1 collision_ids , $' \
          "$WORK/log_header.csv" "the header's trailing fields keep their exact spacing"
# The one column upstream writes WITHOUT the leading space is `#1 lane_id`, and
# it sits between the two literals above, so neither of them sees it. It is the
# column a reimplementation tidying the separator would most plausibly
# normalise, which would make the format change invisible here. Measured
# byte-identical at both ends of the step.
need_grep ', #1 lane_id, #1 lane_offset\[m\] ,' \
          "$WORK/log_header.csv" "the lane_id column keeps its own irregular spacing"

sed -n '8p' "$WORK/log.csv" > "$WORK/f.csv"; need_grep '^0, 0\.000000, Ego, 0, 20\.000000,' "$WORK/f.csv" "first logged frame"
tail -1 "$WORK/log.csv" > "$WORK/f.csv"; need_grep '^601, 30\.050000, Ego, 0, 0\.000000,' "$WORK/f.csv" "last logged frame"

########################## dat2csv with no arguments #########################
# Requirement 15 names the `Usage:` line dat2csv prints when given no
# arguments. It spells the program name from argv[0], which is an absolute
# path here, so the path is matched loosely and the form exactly. Measured
# identical at both ends of the step: exit 255, this one line.
run_tool "$DAT2CSV"
need_status 255 $? "dat2csv with no arguments"
need_grep '^Usage: .*dat2csv <filename>$' "$WORK/last.out" "dat2csv usage line"

############################ the logger line format ##########################
# Requirement 15's "the logger's own line format" obligation: a bracketed
# timestamp, a bracketed level, then the message. Both ends emit the same
# shape, and both emit this particular error on this network.
run_esmini --osc "$FIXTURES/cruise_straight.xosc" --headless \
           --fixed_timestep 0.5 --log_level info
need_status 0 $? "run at log level info"
cp "$WORK/last.out" "$WORK/lvl_info.out"
need_grep '^\[[^]]*\] \[(info|warn|error)\] .' "$WORK/lvl_info.out" "logger line format"

# The floor is the one this project publishes, not a count measured off any one
# tree: an ordinary headless run at the default/info level emits a stream of
# info-tagged lines rather than a token few, and "at least five" is what the
# logging requirement states. Something much larger would be a constant of the
# reference lineage -- how many lines its loading and storyboard inventory
# happens to emit -- and no document fixes that, so a conforming tree whose own
# inventory is thinner must not be failed by it. Five still has teeth: a build
# whose info-level logging has collapsed (a damaged level filter, a funnel that
# sends nothing through the logger, everything logged at debug) cannot reach it.
info_lines=$(grep -c '\[info\]' "$WORK/lvl_info.out")
[ "$info_lines" -ge 5 ] || fail "log level info produced only $info_lines [info] lines"

# --log_level warn keeps the run's [info] lines off stdout. Exactly one [info]
# line survives at v2.46.4 and none at v2.44.2, and the surviving one is the
# config-file discovery notice that this step's OWN new config mechanism emits
# before the requested log level is in force. That one line is therefore
# excluded by name rather than by count, which keeps this assertion exact at
# both ends instead of tolerant at either.
run_esmini --osc "$FIXTURES/cruise_straight.xosc" --headless \
           --fixed_timestep 0.5 --log_level warn
need_status 0 $? "run at log level warn"
cp "$WORK/last.out" "$WORK/lvl_warn.out"
# Both configuration-mechanism notices are excluded by name: the missing-default
# notice and the `Loaded config file` line a resolved default would print. The
# default lookup is declared unscored (instruction requirement 9) and the
# artifact layout is free, so a solver who ships a harmless default config and
# whose relocated `esmini` resolves it must not fail here; neither notice can
# mask a regression, because both are the config mechanism reporting itself.
leaked=$(grep '\[info\]' "$WORK/lvl_warn.out" | grep -vcE 'default config|Loaded config file' || true)
[ "$leaked" = 0 ] || fail "--log_level warn left $leaked of the run's [info] lines on stdout"
need_grep '^\[[^]]*\] \[error\] Unsupported geo reference attr' "$WORK/lvl_warn.out" \
          "warn level still reports errors in the unchanged line format"
