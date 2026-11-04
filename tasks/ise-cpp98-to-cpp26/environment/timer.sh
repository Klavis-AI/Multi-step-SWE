#!/usr/bin/env bash
# The budget this counts down. The step's configured time limit is what
# actually enforces it; this is an advisory copy you can read.
set -u

TEMPLATE_STEP_TIMEOUT_SEC=14400

# The container outlives any one budget, so PID 1's age is not this budget's
# age. Measure the process that started the current step instead, by walking
# to the top of this process tree.
timer_pid=$$
timer_elapsed=''
while :; do
    timer_parent=$(ps -o ppid= -p "$timer_pid" 2>/dev/null | tr -d ' ')
    if ! [[ "$timer_parent" =~ ^[0-9]+$ ]]; then
        break
    fi
    if [ "$timer_parent" -le 1 ]; then
        timer_elapsed=$(ps -o etimes= -p "$timer_pid" 2>/dev/null | tr -d ' ')
        break
    fi
    timer_pid=$timer_parent
done

# The step's configured time limit remains authoritative where process
# ancestry is not visible; this fallback only affects the displayed estimate.
if ! [[ "$timer_elapsed" =~ ^[0-9]+$ ]]; then
    timer_elapsed=$(ps -o etimes= -p 1 2>/dev/null | tr -d ' ')
fi
if ! [[ "$timer_elapsed" =~ ^[0-9]+$ ]]; then
    timer_elapsed=0
fi

timer_remaining=$((TEMPLATE_STEP_TIMEOUT_SEC - timer_elapsed))
if [ "$timer_remaining" -le 0 ]; then
    printf 'Timer expired! The four-hour implementation budget for this step has elapsed.\n'
    exit 0
fi

timer_hours=$((timer_remaining / 3600))
timer_minutes=$(((timer_remaining % 3600) / 60))
timer_seconds=$((timer_remaining % 60))
printf 'Remaining time for this step (hours:minutes:seconds): %d:%02d:%02d\n' \
    "$timer_hours" "$timer_minutes" "$timer_seconds"
