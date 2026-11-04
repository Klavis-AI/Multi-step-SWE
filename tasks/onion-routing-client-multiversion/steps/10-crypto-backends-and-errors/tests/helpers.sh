#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Generated for
# every step by misc/onion-routing-client-multiversion/build-package.sh - keep
# the copies in sync by regenerating, never by hand-editing one of them.
set -u

# Unprivileged identity for candidate code. nobody:nogroup already exists in
# the rust bookworm image.
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

# Give a directory to the unprivileged user. Call before an unprivileged build.
give_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" "$1" 2>/dev/null || true
}

# report <score>
#
#   reward  this step's local score in [0, 1]. Harbor's
#           multi_step_reward_strategy = "mean" averages this key across every
#           step that produced a verifier_result.
#
# Always exits 0: a non-zero exit is a verifier crash, and Harbor then skips
# the remaining steps even without min_reward.
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
    # Reopen to 0755 so a local docker run, which reads this from the host as
    # an ordinary user, can collect it. Still root-owned, so the candidate
    # still cannot write it.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
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

# candidate_crate_root <app>
#
# arti-v0.0.0 keeps its crates at the repository root; every later checkpoint
# keeps them under crates/. Both layouts are documented in TEST_INTERFACE.md
# for the step that uses them, and the driver manifest this step ships names
# the layout this step expects. This helper only reports what is actually
# there, so a layout mismatch is a legible diagnostic rather than a wall of
# "no such file" from cargo.
candidate_crate_root() {
    local app="${1:-/app}"
    if [ -d "$app/crates" ]; then printf 'crates\n'; else printf '.\n'; fi
}
