#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Keep in sync:
# the copies are byte-identical across all five steps.
set -u

# report <score>
#
#   reward  1 when this step's local score in [0, 1] is 1.0, else 0. Harbor's
#           multi_step_reward_strategy = "mean" averages this key across every
#           step that produced a verifier_result.
#
# NO EXTERNAL BINARIES. The candidate's decoder runs in this container as an
# unprivileged user (uid 65533); the scorer itself is root,
# so every program this function would invoke after the decode is a program the
# candidate can replace. Clamping and formatting the reward with awk, say,
# would let three lines of `#!/bin/sh; printf "%.6f" 1` written over
# /usr/bin/awk from inside the decoder forge reward 1.000000, and no workspace
# scan would ever see it, because the attack is not in the workspace.
#
# `printf`, `[[ ]]` and arithmetic below are bash BUILTINS -- resolved inside
# this already-running shell, not looked up on PATH -- so replacing a file on
# disk cannot change what they do.
#
# Always exits 0: a non-zero exit is a verifier crash, and Harbor then skips
# remaining steps even without min_reward.
report() {
    local v="${1:-0}"
    local reward
    # Reject anything that is not a plain decimal before it reaches arithmetic.
    if [[ ! "$v" =~ ^[0-9]+(\.[0-9]+)?$ ]]; then
        v=0
    fi
    # Clamp to [0,1] without invoking a program. bash has no floats, so format
    # with the printf builtin and compare the integer part.
    printf -v reward '%.6f' "$v"
    # reward is now dd.dddddd; clamp by comparing the integer part.
    local int="${reward%%.*}"
    if [[ "$int" -lt 0 ]]; then reward="0.000000"; fi
    if [[ "$int" -ge 1 ]]; then reward="1.000000"; fi
    # Binary step reward: 1 only when the step reward is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
    printf '{"reward":%s}\n' "$reward" > /logs/verifier/reward.json
    # test.sh took /logs/verifier to root 0700 before the decode. Reopen it
    # (still root-owned) so a harbor reading from the host as an ordinary user
    # can collect the reward. Only the reward's readability depends on these;
    # the decode uid cannot write a root-owned 0755/0644 entry either way.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    exit 0
}
