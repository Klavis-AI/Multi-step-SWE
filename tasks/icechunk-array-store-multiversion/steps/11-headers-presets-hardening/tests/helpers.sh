#!/usr/bin/env bash
# Per-step copy: this step's verifier image is built with only this step's
# tests/ directory as the Docker build context, so it cannot COPY from the task
# root or another step. Every copy is identical; keep them in sync.
set -u

# report <score>
#
#   reward  THIS step's own binary reward: 1 when the step's score (the
#           weighted fraction of its scored groups that passed) is 1, else 0.
#           It is never a running total and it never credits a predecessor
#           milestone: task.toml sets multi_step_reward_strategy = "mean", so
#           Harbor averages this key over the eleven steps itself.
#
# There is no min_reward gate. Every step runs to completion and reports its
# own number; no step can abort the trial. The end-to-end checks are one
# ordinary scored group (`gate-endtoend`) inside the score. The full per-group
# breakdown stays in reward_details.json, which is the diagnostic authority.
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

# require_step <the step this tests/ tree belongs to>
#
# Compares the step this IMAGE was built for - baked into /etc/harbor-step-id
# before the build context was copied - with the step this /tests TREE claims.
# They disagree only when the image and the tree came from different steps,
# which is what a build context served from another step's cache produces.
#
# This is the one failure the asset manifest cannot see: a tree that is
# INTERNALLY consistent but belongs to the wrong milestone passes every
# checksum in it, and would be graded by another step's verifier at an
# ordinary-looking score.
#
# reward.json is written NUMERIC-ONLY, through report(). Harbor validates that
# file as dict[str, float | int]; a string field there raises a parse error,
# and a verifier that fails to parse is the crash this design avoids. The
# diagnosis goes to step-mismatch.log and reward_details.json, which harbor
# does not parse.
require_step() {
    local want="$1" got msg
    got=$(cat /etc/harbor-step-id 2>/dev/null) || got=""
    [ -n "$got" ] || got=MISSING
    [ "$got" = "$want" ] && return 0

    msg="VERIFIER/STEP MISMATCH: image is step ${got}, /tests tree is step ${want}"
    printf '%s\n' "$msg" >&2
    mkdir -p /logs/verifier
    printf '%s\n' "$msg" > /logs/verifier/step-mismatch.log
    printf '{"step":"%s","image_step":"%s","score":0.000000,"verifier_error":"step_mismatch","passed_groups":0,"total_groups":0,"groups":{}}\n' \
        "$want" "$got" > /logs/verifier/reward_details.json
    report 0
}
