#!/usr/bin/env bash
# Gate (proof of life). The player still runs a scenario to completion headless and
# stops itself, both before and after this step. The fixture declares its
# vehicles inline and names its road network by a relative path, so nothing
# in the candidate's resources/ takes part.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

# The option is --logfile_path, not --logfile: esmini IGNORES an option it
# does not recognise rather than diagnosing it, so the misspelling used to run
# successfully and then be "confirmed" by the log.txt the default would have
# written anyway. Name a path the default never produces, so that the check
# fails if the option stops being honoured.
run_esmini --osc "$WORK/two_car_brake.xosc" --headless --fixed_timestep 0.05 \
           --logfile_path "$WORK/run.log"
need_status 0 $? "esmini headless run"
need_grep 'Closing' "$WORK/last.out" "esmini shut down cleanly"
need_grep 'StoryboardStop == true' "$WORK/last.out" "the storyboard's own stop trigger fired"
[ -f "$WORK/run.log" ] || fail "--logfile_path did not produce $WORK/run.log"
[ -f "$WORK/log.txt" ] && fail "--logfile_path was ignored: the log went to the default log.txt"

# A scenario file that does not exist is an error, not a silent success.
run_esmini --osc "$WORK/no_such_scenario.xosc" --headless --fixed_timestep 0.05
[ "$?" -eq 0 ] && fail "esmini reported success for a missing scenario file"
exit 0
