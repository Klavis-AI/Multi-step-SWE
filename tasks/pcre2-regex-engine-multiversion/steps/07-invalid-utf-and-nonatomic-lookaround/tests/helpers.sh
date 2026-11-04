#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root.
set -u

SCORE_UID=65534
SCORE_GID=65534

# Lock the reward directory to root. Call this first in test.sh.
#
# Harbor chmods /logs/verifier to 777 just before this container starts, so
# anything running here can overwrite reward.json -- including candidate code
# the verifier builds or runs. Writing the real score last does not help: a
# detached child outlives the scorer and writes after it.
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 700 /logs/verifier
}

# Run candidate-authored code as nobody instead of root. Needed for
# harden_reward_dir to mean anything: root can write a root-owned directory
# whatever its mode.
run_unprivileged() {
    setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        env HOME=/tmp TMPDIR=/tmp "$@"
}

# /app arrives owned by whoever wrote the artifact. Without this the candidate
# build cannot create its output directory, so every candidate scores 0 and the
# step looks merely very hard instead of broken.
give_app_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" /app 2>/dev/null || true
}

# report <score>
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
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    chmod 644 /logs/verifier/reward_details.json 2>/dev/null || true
    chmod 644 /logs/verifier/scorecard.txt 2>/dev/null || true
    exit 0
}

# require_step <NN>
#
# Fails loudly when this step's tests/ tree is not the one baked into this
# step's verifier image. /etc/harbor-step-id is written by the Dockerfile
# before `COPY . /tests`, so it names the *image*; "$1" names the *tree*. A
# mismatch means the COPY layer came out of another step's build cache and
# every check below would grade the wrong milestone -- silently, at a score
# that looks like an ordinary partial result.
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
