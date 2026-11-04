#!/usr/bin/env bash
source /tests/caselib.sh

# The player still runs a scenario to completion headless and stops itself.
# The fixture scenario declares its vehicles inline and names its road network
# by a relative path, so nothing in the candidate's resources/ takes part.
# --logfile_path is used, not --logfile: this release, like the starting one,
# only knows the "_path" spelling.

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

# The log is asked for under a name esmini would never choose on its own.
# The built-in default is log.txt in the working directory, and a case runs with
# its working directory set to $WORK -- so asserting that $WORK/log.txt exists
# would pass with --logfile_path deleted from the command line entirely. Naming
# the file named_run.log and requiring log.txt to be ABSENT is what makes this
# an assertion about the option rather than about esmini's default.
run_esmini --osc "$WORK/two_car_brake.xosc" --headless --fixed_timestep 0.05 --logfile_path "$WORK/named_run.log"
need_status 0 $? "esmini headless run"
need_grep 'Closing' "$WORK/last.out" "esmini shut down cleanly"
need_file "$WORK/named_run.log"
need_grep 'two_car_brake' "$WORK/named_run.log" "the redirected log is the real run log"
[ ! -e "$WORK/log.txt" ] || fail "--logfile_path was ignored: esmini still wrote its default log.txt"

# The log's GRAMMAR, which is the actual content of the carried-over requirement
# and was previously asserted nowhere. Every log ENTRY is `[stamp] [level] message`;
# five lines of the file are not entries and are deliberately not covered by the
# greps below - the four-line build banner at the top and one bare wall-clock line.
# The stamp is the simulation time to exactly three decimals once the scenario is
# running, and EMPTY for entries written before simulation time exists - loading
# the scenario, echoing the resolved option list, announcing the timestep mode.
# Both forms are present in a single headless run at both ends of this step
# (23 empty-bracket and 48 timestamped entries at v2.59.0), so requiring both
# catches a log rewritten to drop either convention.
need_grep '^\[\] \[[a-z]+\] ' "$WORK/named_run.log" "pre-simulation entries carry an empty timestamp bracket"
need_grep '^\[[0-9]+\.[0-9]{3}\] \[[a-z]+\] ' "$WORK/named_run.log" "simulation entries stamp the time to three decimals"
need_grep '^\[[0-9]+\.[0-9]{3}\] \[info\] ' "$WORK/named_run.log" "the level field is a lower-case word, and info is in use"

# The absence check above must come first: the next invocation passes no
# --logfile_path, so it writes the default log.txt into $WORK and would destroy
# the evidence.

# A scenario file that does not exist is an error, not a silent success.
run_esmini --osc "$WORK/no_such_scenario.xosc" --headless --fixed_timestep 0.05
need_status 255 $? "missing scenario file exits -1"
exit 0
