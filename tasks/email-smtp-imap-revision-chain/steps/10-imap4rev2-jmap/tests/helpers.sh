#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Keep in sync.
set -u

# Unprivileged identity for candidate code. nobody:nogroup already exists in
# debian:bookworm-slim; run_suite.py runs every candidate process under it
# (CAND_UID / CAND_GID there).
SCORE_UID=65534
SCORE_GID=65534

# Lock the reward directory to root and pre-write a 0. Call this first.
#
# Harbor chmods /logs/verifier to 777 just before this container starts, so
# anything running here could otherwise overwrite reward.json -- including the
# candidate's CMake build and servers. This runs after Harbor's chmod, so this
# is the mode that sticks. The pre-written 0 means a verifier that dies before
# `report` still leaves a reward behind.
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 700 /logs/verifier
    printf '{"reward":0.000000}\n' > /logs/verifier/reward.json
}

# The fixtures under /tests are the answer key (every script with its recorded
# codes). Root-only before any candidate code runs.
seal_tests() {
    chown -R root:root /tests 2>/dev/null || true
    chmod -R go-rwx /tests
}

# Give /app to the unprivileged user so its build can write build/ and sbin/
# and its servers can write var/. chown -R does not follow symlinks.
give_app_to_unprivileged() {
    mkdir -p /app
    chown -R "$SCORE_UID:$SCORE_GID" /app 2>/dev/null || true
}

# Kill every process of the unprivileged user, looping until none is left, so
# nothing the candidate started is still running when the score is computed.
sweep_unprivileged() {
    ( cd / && timeout -k 5 60 python3 -I /tests/adapter/run_suite.py sweep ) \
        >/dev/null 2>&1 || true
}

# report <score>
#
#   score   this step's local score in [0, 1].
#   reward  binary: 1 when the score is exactly 1, 0 otherwise. Harbor's
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
    [ -n "$reward" ] || reward=0.000000
    mkdir -p /logs/verifier
    # Binary step reward: 1 only when the score is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
    printf '{"reward":%s}\n' "$reward" > /logs/verifier/reward.json
    # Reopen to 0755 so a local docker run, which reads this from the host as
    # an ordinary user, can collect it. Still root-owned, and the candidate uid
    # has been swept, so nothing can write it.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/* 2>/dev/null || true
    exit 0
}
