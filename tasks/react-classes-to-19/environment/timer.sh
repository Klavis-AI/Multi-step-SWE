#!/usr/bin/env bash
# Keep this value equal to the per-step timeout the environment is configured
# with. Every step of this task has the same two-hour budget.
set -u

TEMPLATE_STEP_TIMEOUT_SEC=7200

# The environment is reused from one step to the next. Measuring PID 1 would
# make later steps inherit time spent in earlier steps, so measure the current
# session's exec process by walking to the top of this process tree.
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

# The configured step timeout remains authoritative where process ancestry is
# not visible; this fallback only affects the displayed estimate.
if ! [[ "$timer_elapsed" =~ ^[0-9]+$ ]]; then
    timer_elapsed=$(ps -o etimes= -p 1 2>/dev/null | tr -d ' ')
fi
if ! [[ "$timer_elapsed" =~ ^[0-9]+$ ]]; then
    timer_elapsed=0
fi

timer_remaining=$((TEMPLATE_STEP_TIMEOUT_SEC - timer_elapsed))
if [ "$timer_remaining" -le 0 ]; then
    printf 'Timer expired! The two-hour implementation budget for this step has elapsed.\n'
    exit 0
fi

timer_hours=$((timer_remaining / 3600))
timer_minutes=$(((timer_remaining % 3600) / 60))
timer_seconds=$((timer_remaining % 60))
printf 'Remaining time for this step (hours:minutes:seconds): %d:%02d:%02d\n' \
    "$timer_hours" "$timer_minutes" "$timer_seconds"
