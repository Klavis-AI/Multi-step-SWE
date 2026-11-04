#!/usr/bin/env bash
# Per-step copy: a step's verifier image is built with only that step's tests/
# directory as the Docker build context, so it cannot COPY from the task root
# or from another step. Every copy is byte-identical.
set -u

# require_step <NN>
#
# Docker identifies a local build-context file by (relative path, size,
# mtime-to-the-second) and never reads its bytes, so two steps whose tests/
# trees agree on all three are indistinguishable to the builder and one step's
# layers can be served from another step's cache entry. `--no-cache`, a
# cache-busting ARG and marker files all fail to prevent it. The only defences
# are distinct mtimes or distinct sizes -- and this check, which does not
# prevent the mis-binding but makes it impossible to miss.
#
# /etc/harbor-step-id is written by a literal RUN in the Dockerfile BEFORE the
# first COPY, so it names the IMAGE. This compares it with the number baked
# into the /tests tree. A mismatch reports 0 with an explicit verifier_error
# rather than silently grading the wrong milestone.
require_step() {
    local want="$1" got msg
    got=$(cat /etc/harbor-step-id 2>/dev/null) || got=MISSING
    [ "$got" = "$want" ] && return 0
    msg="VERIFIER/STEP MISMATCH: image is step ${got}, /tests tree is step ${want}."
    printf '%s\n' "$msg" >&2
    mkdir -p /logs/verifier 2>/dev/null || true
    printf '%s\n' "$msg" > /logs/verifier/step-mismatch.log
    printf '{"reward":0,"verifier_error":"step_mismatch","image_step":"%s","tests_step":"%s"}\n' \
        "$got" "$want" > /logs/verifier/reward.json
    exit 0
}

# report <reward>
#
#   reward  THIS step's own reward in [0, 1] -- gate x score, as computed by
#           test.sh. It is never a running total and it never credits a
#           predecessor milestone: task.toml sets
#           multi_step_reward_strategy = "mean", so Harbor averages this key
#           over the fourteen steps itself.
#
# The gate is binary over the GATE groups and earns nothing; the score is the
# weighted fraction of the SCORED groups that passed. Failing any gate group
# therefore zeroes the step outright rather than costing one group's weight,
# and a no-op -- which preserves inherited behaviour but implements nothing
# new -- lands on gate = 1, score = 0, reward = 0.
#
# This function does not decide any of that. It clamps the number test.sh
# hands it and writes a binary step reward: 1 only when that number is exactly
# 1.0, otherwise 0. reward_details.json, written by test.sh, carries the
# fractional gate x score value, the gate verdict and the per-group breakdown
# and is the diagnostic authority.
#
# Always exits 0: a non-zero exit is a verifier crash, and Harbor then skips
# every remaining step even with no min_reward configured.
report() {
    local reward
    reward=$(awk -v v="${1:-0}" 'BEGIN {
        v += 0
        if (v < 0) v = 0
        if (v > 1) v = 1
        printf "%.6f", v
    }')
    mkdir -p /logs/verifier 2>/dev/null || true
    # Binary step reward: 1 only when the step reward is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
    printf '{"reward":%s}\n' "$reward" > /logs/verifier/reward.json
    exit 0
}
