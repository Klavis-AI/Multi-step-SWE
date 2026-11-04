#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Keep in sync.
set -u

# Unprivileged identity for candidate code. nobody:nogroup already exists in
# debian:bookworm-slim.
SCORE_UID=65534
SCORE_GID=65534

# Lock the reward directory to root. Call this first in test.sh.
#
# Harbor chmods /logs/verifier to 777 just before this container starts, so
# anything running here can overwrite reward.json -- including candidate code
# the verifier builds or runs. Writing the real score last does not help: a
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
# directory whatever its mode. Use it everywhere candidate code runs -- a
# Makefile, a linked library's static initializer, a probe compiled against
# candidate source -- not just a service.
run_unprivileged() {
    setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        env HOME=/tmp TMPDIR=/tmp "$@"
}

# Give /app to the unprivileged user. Call before an unprivileged build.
#
# /app arrives owned by whoever wrote the artifact. Without this the build
# cannot create its output directory, so every candidate scores 0 and the step
# looks merely very hard instead of broken.
give_app_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" /app 2>/dev/null || true
}

# report <score>
#
#   reward  this step's local score in [0, 1]. Harbor's
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
# steps, which buries the fault under a second symptom. The reward is 0, but
# `verifier_error` is what a fetched run should be read for -- a plain 0 with
# empty logs is indistinguishable from an agent that did nothing.
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


# ---------------------------------------------------------------------------
# reward = gate x score
# ---------------------------------------------------------------------------
#
# The gate is the inherited-regression and proof-of-life bundle. It is binary
# and earns nothing: it can only veto. The score is the weighted mean over the
# scored groups, whose weights sum to 1.0. A candidate that preserves every
# inherited behaviour and implements nothing new scores 1 x 0 = 0, which is the
# assertion every no-op continuation makes.
#
# report_reward <gate 0|1> <score 0..1> <details-json-path>
report_reward() {
    local gate="$1" score="$2" details="$3" reward
    reward=$(awk -v g="$gate" -v s="$score" 'BEGIN {
        g += 0; s += 0
        if (s < 0) s = 0
        if (s > 1) s = 1
        printf "%.6f", (g == 1 ? s : 0)
    }')
    cp "$details" /logs/verifier/reward_details.json 2>/dev/null || true
    printf '{"reward":%s}\n' "$reward" > /logs/verifier/reward.json
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json /logs/verifier/reward_details.json 2>/dev/null || true
    echo "GATE=$gate SCORE=$score REWARD=$reward"
    exit 0
}

# fail_everything <reason>
#
# One exit path for every abort before scoring can be meaningful: a missing
# build script, a build that did not produce the declared binary, a corrupted
# fixture tree. Writes a reward of 0 with the reason recorded, and still exits
# 0 so Harbor runs the remaining steps and the reason survives in the details.
fail_everything() {
    local reason="$1"
    printf '%s\n' "VERIFIER ABORT: $reason" >&2
    mkdir -p /logs/verifier 2>/dev/null || true
    printf '{"gate":0,"score":0,"reward":0,"abort_reason":%s}\n' \
        "$(printf '%s' "$reason" | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read()))')" \
        > /logs/verifier/reward_details.json
    printf '{"reward":0}\n' > /logs/verifier/reward.json
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json /logs/verifier/reward_details.json 2>/dev/null || true
    exit 0
}
