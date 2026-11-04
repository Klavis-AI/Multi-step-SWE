#!/usr/bin/env bash
# Per-step copy: this step's verifier image is built with only this step's
# tests/ directory as the Docker build context, so it cannot COPY from the task
# root or another step. Every copy is identical; keep them in sync.
set -u

# report <step-local-score>
#
#   reward  this step's own score in [0, 1] - the fraction of this step's
#           scored groups that passed, and nothing else. It is never a running
#           total: task.toml sets multi_step_reward_strategy = "mean", so
#           Harbor averages the `reward` key across all five steps itself.
#           Dividing by the number of task steps, or padding predecessor
#           milestones with a literal 1.000000, would double-count under that
#           strategy and is deliberately gone.
#
# There is no gate multiplier and no min_reward in task.toml: every step runs
# to completion and a weak step costs only its own share of the mean. The
# former gate cases are scored as one ordinary group (`gate-endtoend`), so
# failing them costs exactly one group. reward_details.json keeps the full
# per-group breakdown, including a `gate` key that survives as a diagnostic
# only, and is the diagnostic authority.
#
# Always exits 0: a non-zero exit is a verifier crash, and Harbor then skips
# the remaining steps, which would be indistinguishable from a candidate
# failure.
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
    exit 0
}
