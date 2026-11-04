#!/usr/bin/env bash
# Reports this step's remaining budget.
#
# The configured timeout is authoritative; this script is a convenience so the
# budget is visible from inside the container. BUDGET matches
# steps.agent.timeout_sec in task.toml.
#
# The stamp is per step. The whole ladder runs in one container, so a single
# shared stamp would be created once -- by whichever step ran the script first
# -- and every later step would read it as an already-exhausted budget. This
# copy carries its own step number, and the harness installs the copy belonging
# to the current step, so a step's clock starts the first time its own copy
# runs. Regenerate with environment/assets/gen-timer.py.
set -u
STEP=08
BUDGET=7200
STAMP=/tmp/.step-start-$STEP

if [ ! -f "$STAMP" ]; then
    date +%s > "$STAMP" 2>/dev/null || true
fi

start=$(cat "$STAMP" 2>/dev/null || date +%s)
now=$(date +%s)
elapsed=$(( now - start ))
left=$(( BUDGET - elapsed ))
[ "$left" -lt 0 ] && left=0

printf 'budget   %ds (%dm)\n' "$BUDGET" "$(( BUDGET / 60 ))"
printf 'elapsed  %ds (%dm)\n' "$elapsed" "$(( elapsed / 60 ))"
printf 'left     %ds (%dm)\n' "$left" "$(( left / 60 ))"
