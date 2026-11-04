#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. All six copies
# are identical; edit them together.
set -u

# Unprivileged identity for candidate code. nobody:nogroup already exists in
# python:3.11-slim-bookworm.
SCORE_UID=65534
SCORE_GID=65534

# Wall-clock bounds. Harbor discards a step whose verifier outlives
# [steps.verifier] timeout_sec (3600 s in every step) even when reward.json is
# already written, and then aborts every later step, so a candidate that makes
# this verifier run long would lock in the mean of the steps before it. The
# whole of test.sh runs under VERIFY_BUDGET (see supervise_verifier), which
# leaves room inside the 3600 s for the image build and the artifact round trip
# that harbor's timeout also covers; every candidate invocation runs under
# UNPRIV_TIMEOUT (see run_unprivileged), so one hung phase costs that phase
# rather than the step. The golden runs take 230-430 s per step end to end.
VERIFY_BUDGET=3240
UNPRIV_TIMEOUT=1200

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
    # The first call re-runs the calling test.sh under the deadline and never
    # returns; the re-run lands here again and carries on as before.
    [ -n "${PYPKG_VERIFY_INNER:-}" ] || supervise_verifier "${BASH_SOURCE[1]}"
}

# supervise_verifier <test.sh>
#
# Run the step's test.sh again, as a child, under `timeout VERIFY_BUDGET`, and
# stay behind to finish the step whatever happens to it. On expiry timeout
# sends TERM to the child's process group and KILL 30 s later; candidate
# processes outside that group are uid 65534 and go in the reap. A reward the
# child wrote is kept; with none, the step scores 0. Worst case:
# VERIFY_BUDGET + 30 s + one reap, under timeout_sec - 300 s.
supervise_verifier() {
    local script=$1 rc
    PYPKG_VERIFY_INNER=1 timeout -k 30 "$VERIFY_BUDGET" bash "$script"
    rc=$?
    reap_unprivileged
    if [ ! -s /logs/verifier/reward.json ]; then
        printf '{"reward":0.000000}\n' > /logs/verifier/reward.json
        echo "VERIFIER DEADLINE: test.sh ended with status $rc and no reward" \
             "within ${VERIFY_BUDGET}s -- scoring 0" >&2
    elif [ "$rc" = 124 ] || [ "$rc" = 137 ]; then
        echo "VERIFIER DEADLINE: test.sh was stopped at ${VERIFY_BUDGET}s;" \
             "keeping the reward.json it had already written" >&2
    fi
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/* 2>/dev/null || true
    exit 0
}

# reap_unprivileged -- end every process uid 65534 has in this container.
#
# A signal to pid -1 sent *as* that uid reaches exactly the processes it may
# signal -- in this PID namespace, every candidate process and nothing of
# root's -- in one call. Looped until pgrep (which reads the real ids from
# /proc/<pid>/status) finds none.
reap_unprivileged() {
    local i
    for i in 1 2 3 4 5 6 7 8 9 10; do
        pgrep -u "$SCORE_UID" > /dev/null 2>&1 || return 0
        setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
            kill -9 -1 > /dev/null 2>&1
        sleep 0.2
    done
    pgrep -u "$SCORE_UID" > /dev/null 2>&1 \
        && echo "reap: uid $SCORE_UID still has processes after 10 rounds" >&2
    return 0
}

# Run candidate-authored code as nobody instead of root.
#
# Needed for harden_reward_dir to mean anything: root can write a root-owned
# directory whatever its mode. Use it everywhere candidate code runs -- a build
# backend, a hook file the parser executes, an extension module a probe imports
# -- not just a service.
#
# PYTHONNOUSERSITE because HOME=/tmp is writable by the build backend, which
# runs earlier as this same uid: a usercustomize.py or .pth left in /tmp/.local
# would otherwise load into every later tool whose verdict is scored
# (validate-pyproject, twine, the probes). The reap afterwards is the same
# argument for processes: nothing one call started is still running when the
# next one, or root, reads what it produced. The timeout kills the call's whole
# process group; the reap gets whatever left it.
#
# HOME is a fresh empty directory for every call, removed after it. With a
# shared HOME, one call's candidate code could leave a ~/.config/uv/uv.toml or
# ~/.config/pip/pip.conf that every later call would read -- the offline sync,
# the group installs, the builds whose artifacts are scored -- so a build
# backend could change how a later tool behaved. The caches are not in HOME
# (PIP_CACHE_DIR and UV_CACHE_DIR are set by the image), so nothing a call
# needs is lost.
run_unprivileged() {
    local rc home
    home=$(mktemp -d /tmp/.verifier-home.XXXXXXXX 2>/dev/null) || home=/nonexistent
    [ "$home" = /nonexistent ] || chown "$SCORE_UID:$SCORE_GID" "$home"
    timeout -k 10 "$UNPRIV_TIMEOUT" \
        setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        env HOME="$home" TMPDIR=/tmp PYTHONNOUSERSITE=1 "$@"
    rc=$?
    if [ "$rc" = 124 ] || [ "$rc" = 137 ]; then
        echo "timeout: '$1 ...' was stopped at ${UNPRIV_TIMEOUT}s" >&2
    fi
    reap_unprivileged
    [ "$home" = /nonexistent ] || rm -rf "$home" 2>/dev/null
    return "$rc"
}

# Give a directory to the unprivileged user. Call before an unprivileged build.
#
# The candidate's tree arrives owned by whoever wrote the artifact. Without this
# the build cannot create its output directory, so every candidate scores 0 and
# the step looks merely very hard instead of broken.
give_app_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" "${1:-/app}" 2>/dev/null || true
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
    # Nothing of the candidate's is still running when the score is written.
    reap_unprivileged
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
    # still cannot write it. Everything the step wrote alongside reward.json --
    # the stepNN-* artifacts the run checker reads -- is opened with it.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/* 2>/dev/null || true
    exit 0
}

# ensure_reward
#
# The safety net for the failure mode that is invisible in the mean: a verifier
# that dies before it scores writes no reward.json, and with
# multi_step_reward_strategy = "mean" that step is dropped from the DENOMINATOR
# rather than counted as zero. The run then reports a clean number over a subset
# of the steps. Call this after the scoring body, outside it, so a crash inside
# still produces a 0 that shows up.
ensure_reward() {
    if [ ! -s /logs/verifier/reward.json ]; then
        echo "verifier produced no reward.json -- scoring 0" >&2
        report 0
    fi
}
