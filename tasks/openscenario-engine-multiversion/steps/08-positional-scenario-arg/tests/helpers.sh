#!/usr/bin/env bash
# Per-step copy: this step's verifier image is built with only this step's
# tests/ directory as the Docker build context, so it cannot COPY from the task
# root or another step. Every copy is identical; keep them in sync by
# regenerating with misc/openscenario-engine-multiversion/build-package.sh.
set -u

# report <reward>
#
#   reward  THIS step's own score in [0, 1], computed by test.sh as
#           `gate x score`. It is never a running total and it never credits a
#           predecessor milestone: task.toml sets
#           multi_step_reward_strategy = "mean", so Harbor averages this key
#           over the ten steps itself.
#
#   gate    1 only when every gate group passed. Gate groups are proof of life
#           and inherited regression; they earn nothing and exist to veto, so
#           one gate failure zeroes the step however good the new work is.
#   score   the share-weighted pass fraction over the scored groups alone.
#
# The gate is a veto, not an abort: every group still runs and every step still
# reports a number, so no step can end the trial early. When the gate vetoes,
# reward_details.json names the gate group that did it and why - a step
# reporting 0 without naming its vetoing case is indistinguishable from a
# candidate that did nothing. reward_details.json is the diagnostic authority.
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
        # Binary step reward: 1 only when the step reward is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
printf '{"reward":%s}\n' "$reward" > /logs/verifier/reward.json
    exit 0
}
