#!/usr/bin/env bash
# -h is the short spelling of --help. It must print the same usage text and
# exit without trying to run a scenario.
source /tests/caselib.sh

run_tool "$ESMINI" -h
need_grep "^Usage:.*\\[options\\]" "$WORK/last.out" "-h prints the usage line"
need_grep '(\[--osc\]|--osc) <filename>' "$WORK/last.out" "-h lists the scenario option"
need_grep '\-\-headless' "$WORK/last.out" "-h lists the headless option"

# It is help, not a run: nothing is loaded and nothing is simulated.
need_not_grep 'Loaded ' "$WORK/last.out" "-h did not load a scenario"

# The long spelling still works and agrees with the short one.
run_tool "$ESMINI" -h; cp "$WORK/last.out" "$WORK/short.txt"
run_tool "$ESMINI" --help; cp "$WORK/last.out" "$WORK/long.txt"
diff -q "$WORK/short.txt" "$WORK/long.txt" >/dev/null \
    || fail "-h and --help print different text"
exit 0
