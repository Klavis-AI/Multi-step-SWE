#!/usr/bin/env bash
# The player still runs a scenario to completion headless and stops itself,
# and it still writes its run log where the earlier steps established.
# The fixture declares its vehicles inline and names its road network by a
# relative path, so nothing in the candidate's resources/ takes part.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_esmini --osc "$WORK/two_car_brake.xosc" --headless --fixed_timestep 0.05
need_status 0 $? "esmini headless run"

# The player's own console trace, read back from `esmini` exactly as
# esmini-dyn-play reads it back from `esmini-dyn`: the startup banner, the
# line naming the scenario it opened, and the shutdown line alone on a line
# of its own. All three are quoted in instruction.md §7, bullet 4.
need_grep 'esmini BUILD VERSION' "$WORK/last.out" "esmini identifies itself"
need_grep 'Loading .*two_car_brake\.xosc' "$WORK/last.out" "esmini loaded the scenario"
need_grep '^Closing$' "$WORK/last.out" "esmini shut down cleanly"

# The run log, in both of its documented shapes.
#
# There is no `--logfile` option -- the option is `--logfile_path`, and esmini
# reports an unrecognised argument without failing the run. So asking for a log
# name the default could never produce is the only way to tell redirection from
# the default quietly firing: with CWD set to the case's own directory, a check
# for `log.txt` after passing `--logfile <path>` passes whether or not the
# argument was understood. Each of the two runs below gets its own empty
# directory, because the default name lands in the working directory.
mkdir -p "$WORK/named" "$WORK/dflt"
cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/named/"
cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/dflt/"

( cd "$WORK/named" && run_esmini --osc two_car_brake.xosc --headless \
      --fixed_timestep 0.05 --logfile_path "$WORK/named/named_run.log" )
need_status 0 $? "esmini with --logfile_path"
need_file "$WORK/named/named_run.log"
if [ -e "$WORK/named/log.txt" ]; then
    fail "--logfile_path did not redirect: the default log.txt was written as well"
fi

# With no log option at all the log takes its default name in the working
# directory, and its entries keep the timestamped line format.
( cd "$WORK/dflt" && run_esmini --osc two_car_brake.xosc --headless --fixed_timestep 0.05 )
need_status 0 $? "esmini headless run with no log option"
need_file "$WORK/dflt/log.txt"
need_grep '^[0-9]+\.[0-9]{3}: ' "$WORK/dflt/log.txt" "default run log line format"

# A scenario file that does not exist is an error, not a silent success.
run_esmini --osc "$WORK/no_such_scenario.xosc" --headless --fixed_timestep 0.05
[ "$?" -eq 0 ] && fail "esmini reported success for a missing scenario file"
exit 0
