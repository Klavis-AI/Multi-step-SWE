#!/usr/bin/env bash
# Per-step copy: this step's verifier image is built with only this step's
# tests/ directory as the Docker build context, so it cannot COPY from the task
# root or another step. Every copy is identical; keep them in sync.
set -u

# report <score>
#
#   score   THIS step's own score in [0, 1] - the fraction of this step's
#           scored groups that passed, and nothing else. It is never a running
#           total and it never credits a predecessor milestone.
#   reward  binary: 1 when the score is exactly 1 (every scored group passed),
#           0 otherwise. task.toml sets multi_step_reward_strategy = "mean", so
#           Harbor averages this key over the five steps itself.
#
# Every step runs to completion and reports its own number; no step can abort
# the run. The end-to-end checks are one ordinary scored group
# (`gate-endtoend`). The full per-group breakdown and the fractional score stay
# in reward_details.json, which is the diagnostic authority.
#
# Always exits 0: a non-zero exit is a verifier crash, and Harbor then skips
# the remaining steps even with no min_reward configured.
report() {
    local reward
    reward=$(awk -v v="${1:-0}" 'BEGIN {
        v += 0
        if (v < 0) v = 0
        if (v > 1) v = 1
        printf "%.6f", v
    }')
    # Binary step reward: 1 only when the score is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
    printf '{"reward":%s}\n' "$reward" > /logs/verifier/reward.json
    exit 0
}
