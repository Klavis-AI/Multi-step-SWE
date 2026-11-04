#!/usr/bin/env bash
source /tests/caselib.sh

# A bare filename, with no --osc in front of it, loads and runs the scenario.
# At the starting release the same command line is rejected.

# Two lines of the player's output are not reproducible between runs: the
# timestamp banner and the "Generated seed" line. Everything else is, which is
# what lets requirement 6's "with the same output" be tested directly rather
# than through some proxy string.
norm() {  # norm <captured-output> <destination>
    grep -Ev '^\[[0-9]{4}-[0-9]{2}-[0-9]{2}|seed' "$1" > "$2"
}

run_esmini "$FIXTURES/two_car_brake.xosc" --headless --fixed_timestep 0.05
need_status 0 $? "bare filename with --headless"
need_grep 'Closing' "$WORK/last.out" "run finished"
norm "$WORK/last.out" "$WORK/bare.txt"

# A bare filename and no other arguments at all also works, and the same
# filename still accepted with the old explicit option.
run_esmini "$FIXTURES/two_car_brake.xosc" --headless
need_status 0 $? "bare filename with only --headless"

run_esmini --osc "$FIXTURES/two_car_brake.xosc" --headless --fixed_timestep 0.05
need_status 0 $? "--osc still accepted"
need_grep 'Closing' "$WORK/last.out" "run finished via --osc"
norm "$WORK/last.out" "$WORK/osc.txt"

# Requirement 6: the bare filename "loads and runs exactly the scenario
# --osc <scenario.xosc> would, with the same output and the same exit status".
# Both sides come from the candidate's own binary, so this compares the two
# spellings against each other and not against a recorded transcript.
[ -s "$WORK/bare.txt" ] || fail "normalised output of the bare-filename run is empty"
diff -u "$WORK/osc.txt" "$WORK/bare.txt" > "$WORK/osc_vs_bare.diff" 2>&1 \
    || fail "bare filename and --osc produced different output: $(head -6 "$WORK/osc_vs_bare.diff" | tr '\n' ';')"

# ...and the filename need not lead the command line: requirement 6 allows it
# between options too.
run_esmini --headless "$FIXTURES/two_car_brake.xosc" --fixed_timestep 0.05
need_status 0 $? "bare filename after an option"
norm "$WORK/last.out" "$WORK/mid.txt"
diff -u "$WORK/osc.txt" "$WORK/mid.txt" > "$WORK/osc_vs_mid.diff" 2>&1 \
    || fail "bare filename in second position changed the output: $(head -6 "$WORK/osc_vs_mid.diff" | tr '\n' ';')"

# Requirement 6 also says everything that is NOT the one default argument still
# needs its --name, even when a good scenario filename is on the line. The
# sibling case checks that with the unknown option written FIRST; here it comes
# last, after the default argument has already been consumed, which is the order
# a parser that stops looking once it has found its positional would let slip.
run_esmini "$FIXTURES/two_car_brake.xosc" --headless --no_such_option
need_status 255 $? "unknown option is still rejected next to a valid scenario"
need_grep 'Unrecognized arguments' "$WORK/last.out" "the unknown option is reported"
