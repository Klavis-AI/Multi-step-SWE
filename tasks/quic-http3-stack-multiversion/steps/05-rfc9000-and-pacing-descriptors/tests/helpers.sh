#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. The fifteen
# copies are byte-identical; keep them that way.
set -u

# report <step-local-reward>
#
#   reward  this step's own reward in [0, 1], and nothing else. It is never a
#           running total and never carries credit for an earlier milestone:
#           task.toml sets `multi_step_reward_strategy = "mean"`, so Harbor
#           averages this key across the fifteen steps itself.
#
#           The driver computes it as `gate x score`: the gate is 1 only when
#           every gate group passed — the builds, the end-to-end path, and the
#           inherited milestones — and the score is the weighted fraction of
#           this step's own scored groups that passed. A candidate that carries
#           the previous milestone forward untouched therefore passes the gate
#           and scores zero, which is the point: preserving prior behaviour is
#           necessary, and on its own it is worth nothing.
#
# The value is clamped into [0, 1] and printed with six decimals. Harbor reads
# the `reward` key of /logs/verifier/reward.json as the scalar reward;
# reward_details.json carries the gate verdict, the group that vetoed when the
# gate is 0, and the per-group weights, and remains the diagnostic authority
# for what happened inside a step.
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
        # Binary step reward: 1 only when the step reward is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
printf '{"reward":%s}\n' "$reward" > /logs/verifier/reward.json
    exit 0
}

# require_step <NN>
#
# The verifier/step binding check. /etc/harbor-step-id names the IMAGE; the
# argument names this /tests tree. They disagree only when the Docker build
# context was served from another step's cache entry — the builder keys a local
# context file on (relative path, size, mtime-to-the-second) and never reads its
# bytes — in which case every check that follows would grade the wrong
# milestone at an entirely ordinary-looking score.
#
# Reported, never crashed: a non-zero exit makes Harbor skip the remaining steps
# and buries the fault under a second symptom.
require_step() {
    local want="$1" got msg
    got=$(cat /etc/harbor-step-id 2>/dev/null) || got=MISSING
    [ "$got" = "$want" ] && return 0
    msg="VERIFIER/STEP MISMATCH: image is step ${got}, /tests tree is step ${want}."
    printf '%s\n' "$msg" >&2
    mkdir -p /logs/verifier 2>/dev/null
    printf '%s\n' "$msg" > /logs/verifier/step-mismatch.log
    printf '{"reward":0,"verifier_error":"step_mismatch","image_step":"%s","tests_step":"%s"}\n' \
        "$got" "$want" > /logs/verifier/reward.json
    exit 0
}

# step_score <NN>
#
# This step's local reward on the cumulative worktree, as written by the driver
# to /logs/verifier/reward.json. Provided for parity with the template's helper
# API; the driver computes the value itself and passes it to `report`.
step_score() {
    [ -f /logs/verifier/reward.json ] || { printf '0.000000\n'; return 0; }
    sed -n 's/.*"reward":\([0-9.]*\).*/\1/p' /logs/verifier/reward.json
}
