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

# bounded_unprivileged <seconds> <kill-after> <command...>
#
# run_unprivileged under a deadline whose clock stays root's: `timeout` runs as
# root and only the command it starts drops to the candidate's uid. A clock at
# that uid could be stopped or killed by any candidate process, and the scorer
# could not kill every other process at its uid (toolenv.reap_strays) without
# killing its own deadline with them.
bounded_unprivileged() {
    local secs=$1 grace=$2
    shift 2
    timeout -k "$grace" "$secs" setpriv --reuid "$SCORE_UID" \
        --regid "$SCORE_GID" --clear-groups env HOME=/tmp TMPDIR=/tmp "$@"
}

# snapshot_tree <src> <dest>
#
# A root-owned copy of <src> that nothing at the candidate's uid can change:
# every file readable, nothing writable but by root, no set-id bit, and no
# link or special file that leads back out of the copy -- a link
# into /app or /tmp would let the candidate's processes change what the copy
# says after it was taken. Returns non-zero if the copy is not complete.
snapshot_tree() {
    rm -rf "$2"
    timeout -k 10 120 cp -a "$1" "$2" || return 1
    timeout -k 10 60 python3 -I - "$2" <<'PRUNE' || return 1
import os, stat, sys
top = os.path.realpath(sys.argv[1])
for base, dirs, files in os.walk(top):
    for name in dirs + files:
        path = os.path.join(base, name)
        mode = os.lstat(path).st_mode
        if stat.S_ISLNK(mode):
            target = os.path.realpath(path)
            if target != top and not target.startswith(top + os.sep):
                os.unlink(path)
        elif not (stat.S_ISREG(mode) or stat.S_ISDIR(mode)):
            os.unlink(path)
PRUNE
    chown -R -h root:root "$2" || return 1
    chmod -R u-s,g-s,a+rX,go-w "$2" || return 1
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
#   score   this step's local score in [0, 1]. The reward written is binary:
#           1 only when the score is exactly 1.0, otherwise 0. Harbor's
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
