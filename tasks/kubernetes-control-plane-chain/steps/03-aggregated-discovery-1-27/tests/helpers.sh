#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Keep in sync.
set -u

# The uid every piece of candidate code runs as (nobody): the build, the three
# components and etcd. score.py drops to it; nothing else in here uses it.
SCORE_UID=65534

# Lock the reward directory to root. Called first in test.sh.
#
# Harbor chmods /logs/verifier to 777 just before this container starts, so
# anything running here could otherwise unlink root's reward.json and put its
# own in its place -- including a detached child of the candidate's build or
# components, which outlives the scorer and writes after it. This runs after
# Harbor's chmod, so this is the mode that sticks.
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 700 /logs/verifier
}

# Kill every process of the candidate uid, looping until none is left: a
# respawning chain survives one snapshot, not a loop. `kill -KILL -1` sent AS
# that uid reaches all of them in one pass that new forks cannot race, and
# pkill catches anything that slips through. At most 30 s, and every command in
# it is bounded, so the sweep cannot be what overruns the step.
sweep_candidate_uid() {
    local end=$((SECONDS + 30))
    while [ "$SECONDS" -lt "$end" ]; do
        timeout -k 1 5 setpriv --reuid "$SCORE_UID" --regid "$SCORE_UID" \
            --clear-groups bash -c 'kill -KILL -1' >/dev/null 2>&1 || true
        timeout -k 1 5 pkill -KILL -u "$SCORE_UID" >/dev/null 2>&1 || true
        # a zombie is already dead; only a live process can still write
        timeout -k 1 5 ps -u "$SCORE_UID" -o stat= 2>/dev/null \
            | grep -qv '^Z' || return 0
        sleep 0.5
    done
    echo "WARNING: uid $SCORE_UID still has processes after the sweep" >&2
}

# Reopen the reward directory so a local docker run, which reads it from the
# host as an ordinary user, can collect it. Still root-owned, so nothing the
# candidate could have left behind can write it; the callers sweep first.
release_reward_dir() {
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/*.json /logs/verifier/*.log 2>/dev/null || true
}

# report <score>
#   reward  this step's local score in [0, 1]. Harbor's
#           multi_step_reward_strategy = "mean" averages this key across steps.
# Always exits 0: a non-zero exit is a verifier crash.
report() {
    local reward
    reward=$(awk -v v="${1:-0}" 'BEGIN {
        v += 0
        if (v < 0) v = 0
        if (v > 1) v = 1
        printf "%.6f", v
    }')
    sweep_candidate_uid
    # Binary step reward: 1 only when the step reward is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
    printf '{"reward":%s}\n' "$reward" > /logs/verifier/reward.json
    release_reward_dir
    exit 0
}

# finish: leave with the reward.json score.py (or the provisional write before
# it) left there, after the same sweep and reopen as report.
finish() {
    sweep_candidate_uid
    release_reward_dir
    exit 0
}
