#!/usr/bin/env bash
# STEP_TIMEOUT_SEC below has to match the time limit each step is configured
# with: the timer reports the budget, the runner is what enforces it. Three
# hours.
set -u

STEP_TIMEOUT_SEC=10800

# Measure the process that started the current step, reached by walking to the
# top of this process tree, rather than PID 1. PID 1 is the container's init: it
# has been running since before the current step began, so its elapsed time is
# not the budget.
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

timer_remaining=$((STEP_TIMEOUT_SEC - timer_elapsed))
if [ "$timer_remaining" -le 0 ]; then
    printf 'Timer expired! The three-hour implementation budget has elapsed.\n'
    exit 0
fi

timer_hours=$((timer_remaining / 3600))
timer_minutes=$(((timer_remaining % 3600) / 60))
timer_seconds=$((timer_remaining % 60))
printf 'Remaining time (hours:minutes:seconds): %d:%02d:%02d\n' \
    "$timer_hours" "$timer_minutes" "$timer_seconds"
