#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. The eleven copies
# are byte-identical; keep them that way.
set -u

# report <step-local-score>
#
#   score   this step's own `gate x score` in [0, 1], and nothing else: the
#           gate is binary over this step's inherited regression and proof of
#           life, and the score is the weighted pass fraction over this step's
#           scored groups. It is never a running total and never carries
#           credit for an earlier milestone: task.toml sets
#           `multi_step_reward_strategy = "mean"`, so Harbor averages the
#           reward across the eleven steps itself.
#
# The score is clamped into [0, 1], and the step reward is binary: the
# `reward` key of /logs/verifier/reward.json, which Harbor reads as the scalar
# reward, is 1 only when the score is exactly 1.0 and 0 otherwise.
# reward_details.json carries the fractional score, the gate verdict, the gate
# group that vetoed if one did, and the per-scored-group breakdown, and remains
# the diagnostic authority for what happened inside a step -- a reward of 0
# means nothing on its own until you have read whether the gate passed.
#
# Always exits 0: a non-zero exit is a verifier crash, and Harbor then skips
# the remaining steps and averages only the steps that reported.
report() {
    local reward
    reward=$(awk -v v="${1:-0}" 'BEGIN {
        v += 0
        if (v < 0) v = 0
        if (v > 1) v = 1
        printf "%.6f", v
    }')
    # Binary step reward: 1 only when the step score is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
    printf '{"reward":%s}\n' "$reward" > /logs/verifier/reward.json
    exit 0
}
