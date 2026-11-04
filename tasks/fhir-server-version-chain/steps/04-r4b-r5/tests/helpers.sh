#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Keep in sync.
set -u

# The uid the candidate's build and server run as (useradd in the Dockerfile).
CAND_UID=$(id -u blazeuser 2>/dev/null || echo 4242)

# Take the reward directory back before any candidate code runs.  Harbor chmods
# /logs/verifier to 0777 just before this container starts, and in a 0777
# directory the candidate's uid can unlink root's reward.json and leave its own.
# report() reopens it to 0755, still root-owned, at the very end.
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 700 /logs/verifier
}

# Kill everything the candidate's uid still runs in this container, and keep at
# it until nothing is left: a process the candidate left running is a process
# racing this script, and nothing it does may overlap the reward.
sweep_candidate() {
    local i
    for i in $(seq 1 100); do
        pkill -KILL -U "$CAND_UID" 2>/dev/null
        pkill -KILL -u "$CAND_UID" 2>/dev/null
        pgrep -U "$CAND_UID" >/dev/null 2>&1 || pgrep -u "$CAND_UID" >/dev/null 2>&1 || return 0
        sleep 0.1
    done
}

# report <score> — clamps to [0,1], writes reward.json and exits 0.  A non-zero
# exit is a verifier crash, and Harbor then skips the remaining steps.
report() {
    local reward
    sweep_candidate
    reward=$(awk -v v="${1:-0}" 'BEGIN {
        v += 0
        if (v < 0) v = 0
        if (v > 1) v = 1
        printf "%.6f", v
    }')
        # Binary step reward: 1 only when the step reward is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
printf '{"reward":%s}\n' "$reward" > /logs/verifier/reward.json
    printf 'reward=%s\n' "$reward"
    # Reopen so a reader outside the container can collect it.  Still
    # root-owned, so the candidate still cannot write it.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    exit 0
}
