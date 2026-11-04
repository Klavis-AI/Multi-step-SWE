#!/usr/bin/env bash
# --config_file_path loads a YAML file whose top-level key is the application
# name and whose entries are ordinary options without their leading dashes.
#
# It is not enough to prove the file was READ - the log line alone would be
# satisfied by an implementation that parses the file and ignores it. The
# configured fixed_timestep is therefore observed through its effect: the same
# scenario logged at 0.05 s must produce about twice as many frames as at
# 0.10 s. The ratio is graded rather than either absolute count, so this does
# not depend on the scenario's exact duration.
source /tests/caselib.sh

frames() { grep -c '^[0-9]\+, ' "$1" || true; }

printf 'esmini:\n  fixed_timestep: 0.10\n' > "$WORK/coarse.yml"
printf 'esmini:\n  fixed_timestep: 0.05\n' > "$WORK/fine.yml"

run_esmini --osc "$FIXTURES/two_car_brake.xosc" --headless \
    --config_file_path "$WORK/coarse.yml" --csv_logger "$WORK/coarse.csv"
need_status 0 $? "run with a coarse config file"
need_grep 'Loaded config file' "$WORK/last.out" "config file reported as loaded"
need_grep 'coarse\.yml' "$WORK/last.out" "the loaded file is the one that was asked for"
need_file "$WORK/coarse.csv"
coarse=$(frames "$WORK/coarse.csv")

run_esmini --osc "$FIXTURES/two_car_brake.xosc" --headless \
    --config_file_path "$WORK/fine.yml" --csv_logger "$WORK/fine.csv"
need_status 0 $? "run with a fine config file"
need_grep 'Loaded config file' "$WORK/last.out" "config file reported as loaded"
need_file "$WORK/fine.csv"
fine=$(frames "$WORK/fine.csv")

[ "$coarse" -gt 10 ] || fail "coarse run logged only $coarse frames"
[ "$fine" -gt "$coarse" ] || fail "halving the timestep did not increase the frame count ($fine vs $coarse)"

# Twice the resolution, within one frame of rounding at each end.
awk -v f="$fine" -v c="$coarse" 'BEGIN {
    r = f / c; d = r - 2.0; if (d < 0) d = -d
    exit (d <= 0.15) ? 0 : 1
}' || fail "frame-count ratio $fine/$coarse is not the 2:1 the timesteps imply"

# The other two rules the format section states, which the fixed_timestep runs
# above do not reach: a boolean `true` contributes the bare flag, and entries
# under any other top-level key are ignored. `log_meta_data: true` is the flag
# whose effect is visible from outside -- it turns on the
# `[file::function::line]` tag -- and the foreign key carries a timestep that
# would be obvious in the frame count if it were read.
printf 'esmini:\n  fixed_timestep: 0.10\n  log_meta_data: true\nother_binary:\n  fixed_timestep: 9.99\n' \
    > "$WORK/flags.yml"
run_esmini --osc "$FIXTURES/two_car_brake.xosc" --headless \
    --config_file_path "$WORK/flags.yml" --csv_logger "$WORK/flags.csv"
need_status 0 $? "run with a boolean setting and a foreign top-level key"
need_file "$WORK/flags.csv"
flags=$(frames "$WORK/flags.csv")
[ "$flags" = "$coarse" ] \
    || fail "the foreign top-level key changed the run: $flags frames against $coarse for the same timestep"
need_grep '\[[^]]*\.cpp::[^]]*::[0-9]+\]' "$WORK/last.out" \
    "a boolean `true` in the config file contributed the bare `--log_meta_data` flag"

# A config file that is not there is an error, not a silent default.
run_esmini --osc "$FIXTURES/two_car_brake.xosc" --headless \
    --config_file_path "$WORK/no_such_config.yml"
[ "$?" -eq 0 ] && fail "esmini accepted a missing config file"
exit 0
