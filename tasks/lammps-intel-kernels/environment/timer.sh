#!/usr/bin/env bash
# Advisory step countdown. Print-only: the configured timeout is
# authoritative. The container is reused across steps, so walk to the top of
# this process tree rather than assuming PID 1's start time is this step's.
TEMPLATE_STEP_TIMEOUT_SEC=14400
start=$(date +%s)
top=$PPID
for _ in $(seq 1 12); do
    nxt=$(ps -o ppid= -p "$top" 2>/dev/null | tr -d ' ')
    [ -z "$nxt" ] && break
    [ "$nxt" = "1" ] && break
    [ "$nxt" = "0" ] && break
    top=$nxt
done
ps_start=$(ps -o lstart= -p "$top" 2>/dev/null)
if [ -n "$ps_start" ]; then
    start=$(date -d "$ps_start" +%s 2>/dev/null || echo "$start")
fi
now=$(date +%s)
elapsed=$(( now - start ))
remain=$(( TEMPLATE_STEP_TIMEOUT_SEC - elapsed ))
if [ "$remain" -le 0 ]; then
    echo "Timer expired!"
    exit 0
fi
printf 'Remaining time for this step (hours:minutes:seconds): %d:%02d:%02d\n' \
    $(( remain / 3600 )) $(( (remain % 3600) / 60 )) $(( remain % 60 ))
