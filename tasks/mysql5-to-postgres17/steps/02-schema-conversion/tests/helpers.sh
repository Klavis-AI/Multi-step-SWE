#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Kept in sync by
# authoring/sync_tests.py; edit authoring/common/tests/helpers.sh, never a copy.
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
# The Python scorer has its own copy of this (wglib.deploy.as_user), which is
# what mysqld, postgres, Apache's children and the PHP parser run under. This
# one is here for anything test.sh runs directly.
run_unprivileged() {
    setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        --no-new-privs env HOME=/tmp TMPDIR=/tmp "$@"
}

# Clear set-id bits on every file in the trees restored from the agent's
# container, before any candidate code runs. task.toml restores them with
# `exclude = []`, a path that keeps set-id bits, and a root-owned set-id file
# there would let any candidate account act as root (set-id sweep,
# 2026-10-13). -P: never follow a link out of a tree. Non-zero if a bit
# survives, so the caller can score the step 0 instead of trusting it.
strip_restored_setid() {
    local d
    for d in /app /var/lib/mysql /var/lib/postgresql /etc/mysql /etc/postgresql; do
        [ -d "$d" ] || continue
        find -P "$d" -xdev -type f -perm /6000 -exec chmod ug-s {} + 2>/dev/null
        if [ -n "$(find -P "$d" -xdev -type f -perm /6000 -print -quit 2>/dev/null)" ]; then
            return 1
        fi
    done
    return 0
}

# Give /app to the unprivileged user. Call before an unprivileged build.
#
# /app arrives owned by whoever wrote the artifact. Without this the build
# cannot create its output directory, so every candidate scores 0 and the step
# looks merely very hard instead of broken.
#
# wglib.deploy.prepare_app does the equivalent for www-data, which is the
# account that has to be able to read the tree Apache serves.
give_app_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" /app 2>/dev/null || true
}

# Kill every process in this container that is not root's, until none is left.
# Call after the scorer has finished and before report().
#
# Everything here that is not root is the candidate's: Apache's children and
# PHP as www-data, mysqld as mysql, the cluster (and any program its SQL
# starts) as postgres, the PHP parsers as nobody. The scorer stops the servers
# it started, but a process the candidate detached is in no process group the
# scorer knows about. Each pass signals as the owning uid with kill(-1), which
# the kernel delivers to every process of that uid at once, so a process that
# forks faster than /proc can be read is still caught. Zombies are skipped:
# they cannot run, and nothing in this container reaps them.
sweep_candidate_uids() {
    local round uids uid
    for round in 1 2 3 4 5 6 7 8 9 10; do
        uids=$(for f in /proc/[0-9]*/status; do
                   awk '/^State:/ { z = ($2 == "Z" || $2 == "X") }
                        /^Uid:/   { r = $2; e = $3 }
                        END       { if (!z && r != "") {
                                        if (r != 0) print r
                                        if (e != 0) print e } }' "$f" 2>/dev/null
               done | sort -un)
        [ -n "$uids" ] || return 0
        for uid in $uids; do
            case "$uid" in ''|0|*[!0-9]*) continue ;; esac
            setpriv --reuid "$uid" --regid "$SCORE_GID" --clear-groups \
                bash -c 'kill -s KILL -1' 2>/dev/null
        done
        sleep 1
    done
    return 0
}

# report <score>
#
#   reward  this step's local score in [0, 1]. Harbor's
#           multi_step_reward_strategy = "mean" averages this key across every
#           step that produced a verifier_result.
#
# An empty or non-numeric argument becomes 0, which is what a scorer that died
# before printing anything deserves.
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
    chmod 644 /logs/verifier/*.log /logs/verifier/*.json 2>/dev/null || true
    exit 0
}
