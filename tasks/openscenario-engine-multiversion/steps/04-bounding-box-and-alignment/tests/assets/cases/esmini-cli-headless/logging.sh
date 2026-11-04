#!/usr/bin/env bash
# Gate (proof of life). The player's text log predates this step and its shape does not
# move across it: without --logfile_path the log is written to `log.txt` in
# the working directory, and every message that carries a simulation time is
# written as the time in seconds to three decimal places, then ": ", then the
# message. Measured identical at v2.27.5 and v2.31.10 on this fixture.
#
# This also pins the two logging options that do NOT exist at this release.
# esmini ignores an unrecognised option silently, so the only way to see the
# difference is the usage banner, which is where this checks.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

# No --logfile_path: the log lands on the default name, in the working
# directory, not somewhere else and not nowhere.
( cd "$WORK" && run_esmini --osc "$WORK/two_car_brake.xosc" --headless --fixed_timestep 0.05 )
need_status 0 $? "esmini headless run with no logging options"
need_file "$WORK/log.txt"

need_grep '^esmini BUILD VERSION: ' "$WORK/log.txt" \
    "the log opens with the same build banner the player prints"
need_grep '^0\.000: Starting teleport Action$' "$WORK/log.txt" \
    "a timed message is <seconds to 3 decimals>: <message>"
need_grep '^0\.000: Pos\(20\.00, -1\.53, 0\.00\)' "$WORK/log.txt" \
    "Ego's initial placement is logged at the same coordinates state readback reports"

# The timestamp format is a format, not one lucky line: a substantial part of
# the log is timed, and every timed line uses exactly three decimals.
timed=$(grep -cE '^[0-9]+\.[0-9]{3}: ' "$WORK/log.txt")
[ "$timed" -ge 20 ] || fail "only $timed timed log lines; the run should produce dozens"
odd=$(grep -cE '^[0-9]+\.[0-9]{1,2}: |^[0-9]+\.[0-9]{4,}: ' "$WORK/log.txt")
[ "$odd" -eq 0 ] || fail "$odd log lines carry a timestamp that is not 3 decimal places"

# The finer-grained logging controls arrive after this release and must not be
# invented early: the usage banner is the only place esmini admits what it
# accepts, since it ignores anything it does not know.
run_esmini
need_grep '^  --logfile_path <path>$' "$WORK/last.out" "esmini documents --logfile_path"
need_not_grep '^  --log_level' "$WORK/last.out" \
    "--log_level does not exist at this release and must not be added here"
need_not_grep '^  --log_meta_data' "$WORK/last.out" \
    "--log_meta_data does not exist at this release and must not be added here"
