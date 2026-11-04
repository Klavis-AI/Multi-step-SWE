#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Keep in sync.
set -u

# Unprivileged identity for candidate code. nobody:nogroup already exists in
# debian:bookworm-slim.
SCORE_UID=65534
SCORE_GID=65534

# The pid of the shell that sourced this file, captured now. The safety net
# below fires in that shell and nowhere else.
#
# $BASHPID and not $$: inside a subshell or a command substitution $$ is still
# the invoking shell's pid, so $$ cannot tell a subshell from its parent, while
# $BASHPID is that subshell's own pid.
_REWARD_NET_PID=$BASHPID

# The safety net. However the scorer's shell ends, /logs/verifier/reward.json
# exists and the process exits 0.
#
# test.sh runs under `set -u` with no `set -e`, so one unbound variable would
# otherwise abort it with nothing written -- and a missing reward.json is a
# broken verifier rather than a zero, which is a worse answer than the zero.
#
# Two things this must not do.
#
#   - Overwrite a reward.json that report() already wrote. report() is the
#     normal path and its number is the real one; this writes only when there
#     is nothing there to keep.
#   - Fire anywhere but the shell that armed it. If a shell runs an inherited
#     EXIT trap when a $(...) finishes, an unguarded `exit 0` here truncates
#     that substitution's output and hands a gate a silently wrong answer. The
#     pid guard is what stops that, and it is measured rather than assumed --
#     see the subshell arm of the rescue rehearsal.
#
# It writes through report(), not with a printf of its own, so a rescued run's
# reward.json is the same bytes and the same modes a scored run's is.
_reward_net() {
    local status=$?
    if [ "${BASHPID}" != "${_REWARD_NET_PID}" ]; then
        return 0
    fi
    if [ ! -f /logs/verifier/reward.json ]; then
        echo "VERIFIER-RESCUE: the scorer's shell ended with status ${status} and wrote no reward; writing 0" >&2
        report 0
    fi
    if [ "${status}" -ne 0 ]; then
        echo "VERIFIER-RESCUE: the scorer's shell ended with status ${status} after a reward was written; keeping that reward and exiting 0" >&2
    fi
    exit 0
}

# Lock the reward directory to root, and arm the safety net. Call this first in
# test.sh.
#
# Harbor chmods /logs/verifier to 777 just before this container starts, so
# anything running here can overwrite reward.json -- including candidate code
# the verifier builds or runs. Writing the real score last does not help: a
# detached child outlives the scorer and writes after it.
#
# This runs after Harbor's chmod, so this is the mode that sticks.
#
# The trap is armed here rather than at source time because sourcing this file
# is not the same act as scoring: fixtures/pv-rehearsal.sh sources it at image
# build time and has to go on failing the build with a non-zero status.
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 700 /logs/verifier
    trap _reward_net EXIT HUP INT TERM
    _bound_whole_run
}

# The whole run, under one wall-clock limit.
#
# Every candidate phase has a cap of its own -- the family-A build 1500 s and
# probe 1200 s (runner/score.sh), the inherited suite 900 s, the corroboration
# loop 900 s, step 13's test-JVM-argument export 600 s -- but the caps SUM past
# [steps.verifier] timeout_sec = 3600, and a tree that saturates them jointly
# outlives the verifier. Harbor discards a step whose verifier times out even
# when reward.json exists, drops it from the mean and aborts every later step,
# so that tree would lock in the steps already scored. A container stopped from
# outside runs no EXIT trap, so _reward_net cannot answer for it.
#
# So harden_reward_dir hands the scoring body to a child of this shell: the
# same test.sh, run again under `timeout`, TERM at VERIFY_WALL_SEC and KILL 30 s
# later. timeout signals the child's whole process group, so the child's own
# _reward_net writes 0 if it had not reported yet; if nothing was written at
# all, this shell reports 0. The reward directory stays root-owned throughout,
# so nothing of the candidate's that outlives the child can write it. Worst
# case: 3300 + 30 + report, inside 3600 - 240. The dearest reference run
# measured is ~1060 s (step 13, loaded host), so this is not a limit a correct
# tree can feel; it is the difference between a bounded 0 and a step that
# vanishes from the mean.
VERIFY_WALL_SEC=3300
_bound_whole_run() {
    [ -z "${ARQ_VERIFY_BOUNDED:-}" ] || return 0
    if [ ! -f "$0" ]; then
        echo "VERIFIER-DEADLINE: cannot re-run $0 under a limit; scoring unbounded" >&2
        return 0
    fi
    local rc=0
    ARQ_VERIFY_BOUNDED=1 timeout -k 30 "${VERIFY_WALL_SEC}" bash "$0" || rc=$?
    if [ "${rc}" -eq 124 ] || [ "${rc}" -eq 137 ]; then
        echo "VERIFIER-DEADLINE: the scoring run passed ${VERIFY_WALL_SEC}s and was stopped (rc=${rc})" >&2
    fi
    if [ ! -f /logs/verifier/reward.json ]; then
        echo "VERIFIER-DEADLINE: the scoring run wrote no reward (rc=${rc}); writing 0" >&2
        report 0
    fi
    exit 0
}

# Run candidate-authored code as nobody instead of root.
#
# Needed for harden_reward_dir to mean anything: root can write a root-owned
# directory whatever its mode. Use it everywhere candidate code runs -- a
# Makefile, a linked library's static initializer, a probe compiled against
# candidate source -- not just a service.
#
# And nothing it started outlives it.
# A child the candidate's build or tests leave behind -- `setsid`, a double
# fork, a Gradle daemon -- would otherwise keep running as this user while root
# reads what the stage wrote, and the next stage's evidence too. Every call
# here is in the foreground (none is backgrounded or piped), so when it returns
# no process of this user is meant to be alive, and reap_unprivileged stops
# them all. The command's own status is what the caller sees.
run_unprivileged() {
    local rc=0
    setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        env HOME=/tmp TMPDIR=/tmp "$@" || rc=$?
    reap_unprivileged
    return "$rc"
}

# Stop every process the unprivileged user has in this container, by pid, and
# wait until /proc shows none. Each round stops them all before killing them,
# so a forking child cannot replace itself between the two. Only this
# container's /proc is read. Silent: callers redirect into logs that are read.
reap_unprivileged() {
    local round pid uid pids
    for round in 1 2 3 4 5 6 7 8 9 10; do
        pids=""
        for pid in /proc/[0-9]*; do
            uid=$(awk '/^Uid:/ {print $2; exit}' "$pid/status" 2>/dev/null) \
                || continue
            [ "$uid" = "$SCORE_UID" ] && pids="$pids ${pid#/proc/}"
        done
        [ -n "$pids" ] || return 0
        kill -s STOP $pids 2>/dev/null || true
        kill -s KILL $pids 2>/dev/null || true
        sleep 0.2
    done
    return 0
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
#   <score> is clamped to [0, 1]; the step reward written is binary:
#   1.000000 when the score is exactly 1, otherwise 0.000000.
#
#   reward  this step's reward. Harbor's multi_step_reward_strategy = "mean"
#           averages this key across every step that produced a
#           verifier_result.
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
