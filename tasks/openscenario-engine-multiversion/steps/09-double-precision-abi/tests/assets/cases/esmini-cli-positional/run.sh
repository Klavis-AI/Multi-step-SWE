#!/usr/bin/env bash
# A scenario named as a bare positional argument still loads, and still runs to
# its storyboard end. The path is resolved relative to the working directory,
# and the run is identical to the --osc form.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_esmini "$WORK/two_car_brake.xosc" --headless --fixed_timestep 0.05
need_status 0 $? "esmini with a positional scenario"

# The run reaches the end of the 10 s storyboard.
need_grep 'storyBoard runningState -> stopTransition -> completeState' "$WORK/last.out" \
    "storyboard completion"
# The closing line proves the run shut down cleanly rather than being cut off.
# The timestamp's exact value is not something this step documents - only its
# shape is asserted, so the case does not break if the storyboard end time or
# the timestep ever moves.
need_grep '\[[0-9]+\.[0-9]{3}\] \[info\] Closing' "$WORK/last.out" "clean shutdown"

# R09.27: the log format is unchanged by the widening. Every line emitted once
# the clock is running is `[<seconds to three decimals>] [<level>] <message>`,
# and the three decimals do not widen along with the numeric ABI -- a fourth
# decimal, or a bare integer second, fails here. Asserted as the instruction's
# Notes state it, not as a value.
need_grep '^\[[0-9]+\.[0-9]{3}\] \[(info|warn|error)\] [^ ]' "$WORK/last.out" \
    "log line format"
need_not_grep '^\[[0-9]+\.[0-9]{4,}\]' "$WORK/last.out" \
    "simulation time is not printed with more than three decimals"

# ...and the engine still writes that log to log.txt in the working directory
# by default. The case runs with $WORK as its working directory, so that is
# where the file lands.
need_file "$WORK/log.txt" "default log file"
need_grep '^\[[0-9]+\.[0-9]{3}\] \[(info|warn|error)\] [^ ]' "$WORK/log.txt" \
    "log file carries the same line format"
