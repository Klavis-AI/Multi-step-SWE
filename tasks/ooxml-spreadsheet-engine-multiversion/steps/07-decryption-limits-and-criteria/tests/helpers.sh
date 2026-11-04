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

# install_crash_guard
#
# Fail-closed guard for a fatal exit that never reaches report().
#
# This file and test.sh run under `set -u`, and nothing between here and the
# final report() is wrapped: an unset variable, a missing tool, an OOM kill or a
# full disk kills the script outright. reward.json is then never written at all,
# and a run with no reward.json is indistinguishable from a harness that never
# started -- the same failure mode require_step() exists to avoid, arriving by a
# different door.
#
# Writes a 0 and names the cause instead. Exits 0 for the reason report() does:
# a non-zero exit makes Harbor skip every remaining step, which buries the fault
# under a second symptom.
#
# A no-op on every normal path. report() and require_step() both write
# reward.json before they exit, and this only fires when that file is absent, so
# it can never overwrite a real score.
_crash_guard() {
    local rc=$? cause="${1:-exit}"
    [ -f /logs/verifier/reward.json ] && return 0
    mkdir -p /logs/verifier 2>/dev/null || true
    printf 'VERIFIER CRASH (%s): exited %d before reporting a score.\n' "$cause" "$rc" >&2
    printf '{"reward":0,"verifier_error":"verifier_crash","cause":"%s","exit_code":%d}\n' \
        "$cause" "$rc" > /logs/verifier/reward.json 2>/dev/null || true
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    exit 0
}

# The signals are trapped as well as EXIT. A signal death runs the EXIT trap but
# still exits 128+n, which Harbor reads as a verifier crash and which makes it
# skip the remaining steps; trapping the signal itself lets the handler's
# `exit 0` take effect, so a timed-out or OOM-killed verifier scores 0 and the
# rest of the ladder still runs.
install_crash_guard() {
    trap '_crash_guard exit' EXIT
    trap '_crash_guard signal-HUP'  HUP
    trap '_crash_guard signal-INT'  INT
    trap '_crash_guard signal-TERM' TERM
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
# empty logs is indistinguishable from an agent that did nothing, which is
# exactly how this defect stayed invisible once before.
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
