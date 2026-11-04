#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Keep in sync.
set -u

# report <score>
#
#   reward  this step's local score in [0, 1]. Harbor's
#           multi_step_reward_strategy = "mean" averages this key across every
#           step that produced a verifier_result.
#
# Always exits 0: a non-zero exit is a verifier crash, and Harbor then skips
# remaining steps even without min_reward.
report() {
    local reward
    reward=$(awk -v v="${1:-0}" 'BEGIN {
        v += 0
        if (v < 0) v = 0
        if (v > 1) v = 1
        printf "%.6f", v
    }')
        # Binary step reward: 1 only when the step reward is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
printf '{"reward":%s}\n' "$reward" > /logs/verifier/reward.json
    printf '{"scored":true,"reward":%s}\n' "$reward" > /logs/verifier/scored.json
    exit 0
}

# sweep_candidate: kill every process of the unprivileged `candidate` account
# test.sh builds and scores the submission as, and keep killing until none is
# left (bounded). Called after the build, after the scorer, and on the
# whole-run bound's expiry path: a double-forked child escapes every
# process-group kill, and nothing of the candidate's may still be running while
# the tree is gated or the reward is decided. Runs inside the verifier
# container only, where that account exists for the candidate alone.
sweep_candidate() {
    local _i
    id candidate >/dev/null 2>&1 || return 0
    for _i in $(seq 1 50); do
        pkill -KILL -u candidate 2>/dev/null || true
        pgrep -u candidate >/dev/null 2>&1 || return 0
        sleep 0.2
    done
    echo "verifier: processes of candidate survived the sweep" >&2
    return 0
}
