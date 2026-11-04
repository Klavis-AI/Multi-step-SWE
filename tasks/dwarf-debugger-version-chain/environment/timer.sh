#!/usr/bin/env bash
# Advisory countdown for the current step. The step's configured timeout is
# the enforcing limit.
#
# Every step reuses this container, so neither PID 1's age nor procfs's own
# timestamps (`stat /proc/self` reads back the container's start) is this
# step's age: later steps would inherit the time earlier steps used. Each
# step's session is a fresh exec into the container, so measure the process at
# the top of this script's own ancestry instead.
set -u
BUDGET=${BUDGET_SEC:-7200}
pid=$$
elapsed=''
while :; do
    parent=$(ps -o ppid= -p "$pid" 2>/dev/null | tr -d ' ')
    if ! [[ "$parent" =~ ^[0-9]+$ ]]; then
        break
    fi
    if [ "$parent" -le 1 ]; then
        elapsed=$(ps -o etimes= -p "$pid" 2>/dev/null | tr -d ' ')
        break
    fi
    pid=$parent
done
# Fallbacks for a runner that hides the ancestry; they only change the estimate.
if ! [[ "$elapsed" =~ ^[0-9]+$ ]]; then
    elapsed=$(ps -o etimes= -p 1 2>/dev/null | tr -d ' ')
fi
if ! [[ "$elapsed" =~ ^[0-9]+$ ]]; then
    elapsed=0
fi
left=$(( BUDGET - elapsed ))
if [ "$left" -lt 0 ]; then left=0; fi
printf 'agent phase: %d s elapsed, %d s of %d s remaining\n' "$elapsed" "$left" "$BUDGET"
