#!/usr/bin/env bash
# Step verifier entry point.  Whatever happens, /logs/verifier/reward.json must
# exist and the exit status must be 0: a crashed verifier would make Harbor skip
# the remaining steps.
set -u
# Every command run as root here and in runner.py is found through this PATH: root-owned
# system directories only, none of which the candidate uid can write.
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
LOG=/logs/verifier
mkdir -p "$LOG"

# The unprivileged account every piece of candidate code runs as (see runner.py).
CAND_UID=65534

# sweep_candidate: SIGKILL every process of the candidate account, looping until none is
# left. kill(-1) issued *as* that account reaches exactly the processes it owns -- a build
# child, a case under test, a gdbserver left running -- and nothing else. Defence in depth:
# the reward dir is already root-owned and the score is derived by root from /work, so a
# surviving child cannot forge a reward; this just makes sure none outlives the score.
sweep_candidate() {
    local i
    command -v setpriv >/dev/null 2>&1 || return 0
    for i in $(seq 1 50); do
        setpriv --reuid="$CAND_UID" --regid="$CAND_UID" --clear-groups \
            bash -c 'kill -KILL -1' >/dev/null 2>&1
        pgrep -u "$CAND_UID" >/dev/null 2>&1 || return 0
        sleep 0.1
    done
    return 0
}

_last_resort() {
    sweep_candidate
    if [ ! -s "$LOG/reward.json" ]; then
        printf '{"reward":0.0}\n' > "$LOG/reward.json"
        echo "verifier exited without reporting; wrote reward 0" >> "$LOG/report.txt"
    fi
    exit 0
}
trap _last_resort EXIT

umask 0022

# GKE creates /logs/verifier world-writable.  Candidate code must never be able
# to write its own reward: lock the directory down before anything of the
# candidate's runs, and run that code unprivileged (see runner.py).
chown root:root "$LOG" 2>/dev/null || true
chmod 0755 "$LOG" 2>/dev/null || true

# Provisional zero before the scored work: if the runner is cut off -- a candidate that stages
# a huge tree to stall staging, or a build/scoring hang -- Harbor discards a verifier that
# overruns `[steps.verifier] timeout_sec` (3600 s) AND aborts every later step, so the whole
# runner is held under one wall-clock bound well below that ceiling and this zero stands when
# it fires. The reference oracle completes in well under 900 s, so the bound never cuts it.
printf '{"reward":0.0}\n' > "$LOG/reward.json"

export LC_ALL=C TZ=UTC
timeout -k 10 3350 python3 -I /tests/runner.py 2>&1 | tail -400
sweep_candidate
_last_resort
