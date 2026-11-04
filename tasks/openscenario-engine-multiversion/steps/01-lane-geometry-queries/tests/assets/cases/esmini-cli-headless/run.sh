#!/usr/bin/env bash
# The player still runs a scenario to completion headless and stops itself.
# The fixture scenario declares its vehicles inline and names its road network
# by a relative path, so nothing in the candidate's resources/ takes part.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

# The option is spelled --logfile_path; --logfile does not exist at this
# release. That distinction is load-bearing here: the default log name is
# log.txt in the current working directory, and a case runs with CWD set to its
# own $WORK, so asserting that "$WORK/log.txt" appeared would pass whether the
# option was honoured or ignored. The redirect target below is deliberately a
# name the default can never produce.
run_esmini --osc "$WORK/two_car_brake.xosc" --headless --fixed_timestep 0.05 \
           --logfile_path "$WORK/named_run.log"
need_status 0 $? "esmini headless run"
need_grep 'Closing' "$WORK/last.out" "esmini shut down cleanly"
[ -f "$WORK/named_run.log" ] || fail "--logfile_path did not produce $WORK/named_run.log"
[ -f "$WORK/log.txt" ] && fail "--logfile_path was ignored: the default log.txt was written instead"

# Entries carry the simulation time, formatted %.3f followed by ": ".
need_grep '^[0-9]+\.[0-9]{3}: ' "$WORK/named_run.log" "log entry timestamp format"

# A scenario file that does not exist is an error, not a silent success.
run_esmini --osc "$WORK/no_such_scenario.xosc" --headless --fixed_timestep 0.05
[ "$?" -eq 0 ] && fail "esmini reported success for a missing scenario file"

# The option parser still rejects an option it does not know. There is no
# --trace at this release and the instruction states the rejection literally,
# so it is asserted rather than assumed. Only the message is checked: the
# instruction does not pin the run's exit status for a rejected option.
run_esmini --osc "$WORK/two_car_brake.xosc" --headless --fixed_timestep 0.05 --trace
need_grep 'Unrecognized arguments:' "$WORK/last.out" "esmini rejects an unknown option"
exit 0
