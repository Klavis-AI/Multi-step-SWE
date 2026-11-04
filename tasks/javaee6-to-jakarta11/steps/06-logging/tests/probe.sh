#!/bin/bash
# Step 06's runtime probe: settle the audit log, then report what is on disk.
#
# run_behaviour.sh calls this once, after drive.py has finished all 428
# observations and while every server instance is still up. It is the only
# place in the verifier that can see a running process: score.py runs after
# the EXIT trap, and that trap sends SIGTERM, waits three seconds, and then
# sends SIGKILL.
#
# Why this step needs one at all. The gate this rung is built around --
# "the ssh audit log is still written" -- reads a file that a logging backend
# is free to buffer. Today's backend does not: measured on the step-05
# reference (NOTES §11.26), the three sites' `logs/sshd_log` are byte-for-byte
# identical read here and read again after teardown, because log4j 1.2's
# AsyncAppender(blocking=true) in front of a DailyRollingFileAppender with
# immediateFlush=true has the tail on disk before the suite ends. Logback's
# AsyncAppender is the one this rung asks for, and it drains its queue in
# stop() under a bounded maxFlushTime. Three seconds is a generous budget for
# a queue that is at most a few hundred short lines -- and it is also exactly
# the harness's SIGKILL deadline. A candidate must not be charged for losing
# a race with the teardown it does not control, so this probe waits for the
# file to stop moving while the process is still alive and unhurried.
#
# What this probe does NOT do: judge. It asserts nothing, it fails nothing,
# and its exit status is about its own mechanics, not about what it found --
# "the log is empty" is a perfectly successful probe run and a perfectly
# failed gate. score.py reads /score/site-*/logs/sshd_log itself and decides;
# this report is corroboration, and the corroboration is worth having because
# it was taken at a moment score.py cannot reach. Keeping the judgement in one
# file also keeps the two from drifting: there is one definition of "written",
# and it is in score.py.
#
# Output is tab-separated key/value lines on stdout, which the caller
# redirects to /score/log/probe.log. Parsed by score.py; readable by a person.

PROFILES="main alt ssl"
LOG_NAME="sshd_log"

# Stability window. Three consecutive identical sizes, 400 ms apart, is the
# settle condition; 50 polls is the ceiling, so a file that never stops
# growing costs 20 s rather than the whole verifier timeout. Both numbers are
# bounded above by run_behaviour.sh's own budget and below by the observation
# that the reference's file is already final when the first read happens, so
# a correct tree pays two polls and less than a second per site.
STABLE_READS=3
POLL_SECONDS=0.4
MAX_POLLS=50

size_of() {
    # stat, not `wc -c`, so that a missing file is distinguishable from an
    # empty one: an absent file reports -1 and an empty one reports 0.
    if [ -f "$1" ]; then
        stat -c %s "$1" 2>/dev/null || echo -1
    else
        echo -1
    fi
}

printf 'probe.version\t1\n'
printf 'probe.started\t%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"

for p in $PROFILES; do
    site="/score/site-$p"
    log="$site/logs/$LOG_NAME"

    prev=""
    same=0
    polls=0
    while [ "$polls" -lt "$MAX_POLLS" ]; do
        cur="$(size_of "$log")"
        if [ "$cur" = "$prev" ]; then
            same=$((same + 1))
            [ "$same" -ge "$((STABLE_READS - 1))" ] && break
        else
            same=0
        fi
        prev="$cur"
        polls=$((polls + 1))
        sleep "$POLL_SECONDS"
    done

    bytes="$(size_of "$log")"
    if [ "$bytes" -lt 0 ] 2>/dev/null; then
        state="missing"
        lines=0
        bytes=0
    elif [ "$polls" -ge "$MAX_POLLS" ]; then
        state="growing"
        lines=$(wc -l < "$log" 2>/dev/null || echo 0)
    else
        state="settled"
        lines=$(wc -l < "$log" 2>/dev/null || echo 0)
    fi

    printf 'probe.site\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$p" "$log" "$lines" "$bytes" "$state" "$polls"
done

# One line of context per site, so that a failure report says what the file
# looked like and not only how big it was. Truncated hard: this goes into
# gate evidence, and evidence that can grow without bound is a report that
# cannot be read.
for p in $PROFILES; do
    log="/score/site-$p/logs/$LOG_NAME"
    if [ -s "$log" ]; then
        printf 'probe.first\t%s\t%s\n' "$p" "$(head -1 "$log" | cut -c1-200)"
        printf 'probe.last\t%s\t%s\n' "$p" "$(tail -1 "$log" | cut -c1-200)"
    fi
done

# What else is in each logs directory. A candidate who points the appender at
# `sshd_log.0` or `audit.log` fails the gate in score.py; this is what tells
# the author's eye which of the two mistakes it was.
for p in $PROFILES; do
    d="/score/site-$p/logs"
    if [ -d "$d" ]; then
        printf 'probe.logsdir\t%s\t%s\n' "$p" \
            "$(ls -1 "$d" 2>/dev/null | head -20 | tr '\n' ' ')"
    else
        printf 'probe.logsdir\t%s\t(no logs directory)\n' "$p"
    fi
done

printf 'probe.done\t%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
exit 0
