#!/usr/bin/env bash
# The same configuration can arrive through the ESMINI_CONFIG_FILE environment
# variable instead of the command line. The engine announces that it found the
# variable, and the settings in the file take effect the same way.
source /tests/caselib.sh

frames() { grep -c '^[0-9]\+, ' "$1" || true; }

printf 'esmini:\n  fixed_timestep: 0.10\n' > "$WORK/env_coarse.yml"
printf 'esmini:\n  fixed_timestep: 0.05\n' > "$WORK/env_fine.yml"

export ESMINI_CONFIG_FILE="$WORK/env_coarse.yml"
run_esmini --osc "$FIXTURES/two_car_brake.xosc" --headless --csv_logger "$WORK/env_coarse.csv"
rc=$?
unset ESMINI_CONFIG_FILE
need_status 0 "$rc" "run configured from the environment"
need_grep 'Found environment variable ESMINI_CONFIG_FILE:' "$WORK/last.out" \
    "the environment variable is announced"
need_grep 'env_coarse\.yml' "$WORK/last.out" "the announced path is the one that was set"
need_file "$WORK/env_coarse.csv"
coarse=$(frames "$WORK/env_coarse.csv")

export ESMINI_CONFIG_FILE="$WORK/env_fine.yml"
run_esmini --osc "$FIXTURES/two_car_brake.xosc" --headless --csv_logger "$WORK/env_fine.csv"
rc=$?
unset ESMINI_CONFIG_FILE
need_status 0 "$rc" "second run configured from the environment"
need_file "$WORK/env_fine.csv"
fine=$(frames "$WORK/env_fine.csv")

[ "$coarse" -gt 10 ] || fail "coarse run logged only $coarse frames"
[ "$fine" -gt "$coarse" ] || fail "the environment-supplied timestep had no effect ($fine vs $coarse)"
awk -v f="$fine" -v c="$coarse" 'BEGIN {
    r = f / c; d = r - 2.0; if (d < 0) d = -d
    exit (d <= 0.15) ? 0 : 1
}' || fail "frame-count ratio $fine/$coarse is not the 2:1 the timesteps imply"

# With the variable unset the announcement must not appear, so the message is
# evidence of the variable and not something printed on every run.
run_esmini --osc "$FIXTURES/two_car_brake.xosc" --headless --fixed_timestep 0.05
need_status 0 $? "run with no config in the environment"
need_not_grep 'Found environment variable ESMINI_CONFIG_FILE:' "$WORK/last.out" \
    "the announcement is tied to the variable"
exit 0
