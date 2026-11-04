#!/usr/bin/env bash
# Keep this value synchronized with every [steps.agent].timeout_sec entry in
# task.toml. The runnable template gives each step a two-hour budget.
set -u

TEMPLATE_STEP_TIMEOUT_SEC=7200

# Multi-step tasks reuse their environment. Measuring PID 1 would make
# later steps inherit time spent in earlier steps, so measure the current
# agent-phase exec process by walking to the top of this process tree.
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

# The configured timeout remains authoritative for runners that do not
# expose process ancestry; this fallback only affects the displayed estimate.
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
