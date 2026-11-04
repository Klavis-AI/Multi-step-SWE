#!/usr/bin/env bash
# Per-step copy: this step's verifier image is built with only this step's
# tests/ directory as the Docker build context, so it cannot COPY from the task
# root or from another step. Every copy is byte-identical; a drift between
# copies is a defect, not a variant.
set -u

# Unprivileged identity for candidate code. nobody:nogroup already exists in
# the pinned base image.
SCORE_UID=65534
SCORE_GID=65534

# Lock the reward directory to root. Call this first in test.sh.
#
# Harbor chmods /logs/verifier to 777 just before this container starts, so
# anything running here can overwrite reward.json -- including candidate code
# the verifier builds and runs, and this verifier runs a great deal of it: a
# cargo build with candidate build scripts, a BoGo shim driven by a runner, a
# TLS client and a TLS server. Writing the real score last does not help: a
# detached child outlives the scorer and writes after it.
#
# This runs after Harbor's chmod, so this is the mode that sticks.
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 700 /logs/verifier
}

# Run candidate-authored code as nobody instead of root.
#
# Needed for harden_reward_dir to mean anything: root can write a root-owned
# directory whatever its mode. Used for every cargo invocation and for every
# process that is, or spawns, a binary built from /app -- including the BoGo
# runner, which spawns the candidate's shim once per case.
run_unprivileged() {
    setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        env HOME=/tmp TMPDIR=/tmp \
            CARGO_HOME="$CARGO_HOME" RUSTUP_HOME="${RUSTUP_HOME:-/usr/local/rustup}" \
            PATH="$PATH" CARGO_TERM_COLOR=never "$@"
}

# Give /app and the build scratch to the unprivileged user. Call before the
# first unprivileged build.
#
# /app arrives owned by whoever wrote the artifact. Without this the build
# cannot create target/, so every candidate scores 0 and the step looks merely
# very hard instead of broken.
give_app_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" /app 2>/dev/null || true
}

# report <step-local-reward>
#
#   reward  this step's own reward in [0, 1], and under `reward = gate x score`
#           that is a product, not a pass fraction:
#
#             gate   in {0,1}  every GATE group passes, or the step is zero
#             score  in [0,1]  weighted pass fraction over the SCORED groups
#
#           GATE groups are proof of life plus the inherited end-to-end paths
#           this step carries forward. They earn nothing and only veto. SCORED
#           groups are this step's own new capability; their weights sum to 1.0
#           among themselves and are declared in data/groups.tsv, which is the
#           manifest the scorer reads. A continuation that preserves the
#           inherited work and implements nothing new therefore scores exactly
#           0 -- gate 1, score 0 -- with no per-step ceiling to compute.
#
#           It is never a running total: task.toml sets
#           multi_step_reward_strategy = "mean", so Harbor averages the
#           `reward` key across all thirteen steps itself.
#
# finish() in test.sh computes the product and writes the per-group breakdown
# to reward_details.json, which is the diagnostic authority and which names the
# vetoing group whenever the gate verdict is `fail`.
#
# Always exits 0: a non-zero exit is a verifier crash, and Harbor then skips the
# remaining steps, which would be indistinguishable from a candidate failure.
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
    # Reopen to 0755 so a local docker run, which reads this from the host as an
    # ordinary user, can collect it. Still root-owned, so candidate code still
    # cannot write it.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/*.json /logs/verifier/*.txt 2>/dev/null || true
    exit 0
}

# require_step <NN>
#
# Fails loudly when this step's tests/ tree is not the one baked into this
# step's verifier image. /etc/harbor-step-id is written by the Dockerfile before
# `COPY . /tests`, so it names the *image*; "$1" names the *tree*. A mismatch
# means the COPY layer came out of another step's build cache and every check
# below would grade the wrong milestone -- silently, at a score that looks like
# an ordinary partial result.
#
# Reports instead of crashing: a non-zero exit makes Harbor skip the remaining
# steps, which buries the fault under a second symptom.
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
