#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root.  Identical copies
# of this file live in every steps/NN-*/tests/; keep them in sync.
#
# Besides the privilege primitives, this file installs an exit trap: a
# verifier that dies before it scores must still leave a reward behind,
# because `multi_step_reward_strategy = "mean"` averages only the steps that
# wrote one and a crashed step otherwise raises the mean by leaving it.
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
#
# HOME is the application's own HOME and not /tmp: Bundler writes there, and a
# HOME that differs from the one the goldens were recorded under adds a
# Bundler line to the stdout of every rake task.  APP_HOME is exported by
# test.sh from the same `cvh.runner.app_env()` the recorder uses.
run_unprivileged() {
    unpriv_argv
    "${UNPRIV[@]}" "$@"
}

# UNPRIV -- the same drop, as an array, for a caller that has to exec the
# command itself.  `timeout` runs a program and not a shell function, so
# `timeout 600 run_unprivileged ruby ...` fails with "command not found" and
# the phase it was bounding reports a tool error instead of a measurement.
# There is one spelling of the drop and both entry points use it.
unpriv_argv() {
    UNPRIV=(setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups
            env "HOME=${APP_HOME:-/tmp}" TMPDIR=/tmp)
}

# Give /app to the unprivileged user. Call before an unprivileged build.
#
# /app arrives owned by whoever wrote the artifact. Without this the build
# cannot create its output directory, so every candidate scores 0 and the step
# looks merely very hard instead of broken.
give_app_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" /app 2>/dev/null || true
}

# sweep_uid <uid> -- kill every process of one uid, until none is left.
#
# A phase's `timeout` kills its own process group, and a process the candidate
# detached (setsid, a double fork) is in no group anything here knows about.
# While it lives it can rewrite whatever its uid can write, and append to any
# log it inherited.  So `run` calls this after every phase, and every reading
# is taken after the processes that produced it are gone.  Each pass signals
# as that uid with kill(-1), which the kernel delivers to all of its processes
# at once, so one that forks faster than /proc can be read is caught as well.
# One uid only: the database server runs as postgres and has to outlive every
# phase.
sweep_uid() {
    local uid="$1" round
    case "$uid" in ''|0|*[!0-9]*) return 0 ;; esac
    for round in 1 2 3 4 5 6 7 8 9 10; do
        _uid_alive "$uid" || return 0
        setpriv --reuid "$uid" --regid "$SCORE_GID" --clear-groups \
            bash -c 'kill -s KILL -1' 2>/dev/null
        sleep 0.2
    done
    return 0
}

# _uid_alive <uid> -- is any process that is not a zombie running as <uid>?
# Plain bash, no fork per process: `run` asks this after every phase.
_uid_alive() {
    local want="$1" f line state ids
    for f in /proc/[0-9]*/status; do
        state="" ids=""
        while IFS= read -r line; do
            case "$line" in
                State:*) state=$line ;;
                Uid:*)   ids=$line; break ;;
            esac
        done 2>/dev/null < "$f" || continue
        # shellcheck disable=SC2086
        set -- $state
        case "${2:-}" in Z|X) continue ;; esac
        # shellcheck disable=SC2086
        set -- $ids
        [ "${2:-}" = "$want" ] || [ "${3:-}" = "$want" ] && return 0
    done
    return 1
}

_CV_REPORTED=0

# _write_reward <score> -- the clamp and the file, without the exit.
_write_reward() {
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
    _CV_REPORTED=1
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
    _write_reward "${1:-0}"
    exit 0
}

# Nothing else in test.sh may exit without a reward.  `set -e`, a failed
# `bundle install`, an OOM kill of the scorer, a timeout that fires while the
# suite is running: each of those leaves the step with no reward.json, and a
# step with no reward.json is not a zero, it is absent from the mean.  Two
# steps at 0.0 and one crash average to 0.0; two steps at 0.0 and one *absent*
# average to 0.0 as well -- but ten steps at 0.9 and three crashes average to
# 0.9, which is the number this trap exists to prevent.
_cv_on_exit() {
    local status=$?
    if [ "$_CV_REPORTED" = 0 ]; then
        echo "verifier exited without scoring (status $status): reward 0" >&2
        _write_reward 0
    fi
    exit 0
}
trap _cv_on_exit EXIT

# say <words...> -- one timestamped line, to stderr so it interleaves with the
# commands' own output in the order it happened.  `$SECONDS` is bash's own
# counter and not a wall time -- the application's clock starts at 2016-01-19
# and ticks, and this shell is outside the preload anyway.
# The stamp is here to show which phase of a 30 minute verifier a failure
# landed in.
say() {
    printf '=== [%6ss] %s\n' "$SECONDS" "$*" >&2
}
