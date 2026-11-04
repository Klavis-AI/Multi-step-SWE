#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Keep in sync.
set -u

# Unprivileged identity for candidate code. nobody:nogroup already exists in
# ubuntu:22.04, and util-linux is installed for setpriv.
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
    _bound_whole_run
}

# The whole run, under one wall-clock limit.
#
# The replay bounds each job at 900 s, but nothing bounds the run: the Maven
# builds, the gate's build and pom rounds and the inherited suite run the
# candidate's pom with no limit of their own, and 140 jobs at their per-job
# limit are hours. Harbor discards a step whose verifier outlives
# [steps.verifier] timeout_sec = 3600 even when reward.json exists, drops it
# from the mean and aborts every later step -- so a tree that hangs here would
# lock in the steps already scored instead of scoring this one.
#
# So harden_reward_dir hands the scoring body to a child of this shell: the
# same test.sh, run again under `timeout`, TERM at VERIFY_WALL_SEC and KILL 30 s
# later (timeout signals the child's whole process group). If the child wrote
# no reward, this shell reports 0. The reward directory is root-owned 0700 the
# whole time, so nothing of the candidate's can write it meanwhile. Worst case:
# 3300 + 30 + report, inside 3600 - 240. The dearest reference run measured is
# 1801 s (oracle, step 5, loaded host), so a correct tree does not feel this.
VERIFY_WALL_SEC=3300
_bound_whole_run() {
    [ -z "${MMTF_VERIFY_BOUNDED:-}" ] || return 0
    if [ ! -f "$0" ]; then
        echo "VERIFIER-DEADLINE: cannot re-run $0 under a limit; scoring unbounded" >&2
        return 0
    fi
    local rc=0
    MMTF_VERIFY_BOUNDED=1 timeout -k 30 "${VERIFY_WALL_SEC}" bash "$0" || rc=$?
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
# The environment carries through, which the Maven builds below need: MAVEN_ARGS
# points at the warm repository under /scratch, and JAVA_HOME and SPARK_HOME
# name the rung's toolchain. Only HOME and TMPDIR are replaced, because nobody
# has neither and Maven writes to both.
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

# A directory the unprivileged half writes its results into, created by root so
# that root still owns the parent. Every path the scorer later reads back is
# made this way rather than by the build: a results directory the candidate owns
# is a results directory the candidate can replace wholesale.
give_dir_to_unprivileged() {
    mkdir -p "$1"
    chown "$SCORE_UID:$SCORE_GID" "$1" 2>/dev/null || true
}

# Stop every process the unprivileged user still has in this container, by
# pid, and wait until /proc shows none. The replay and the inherited suite run
# the candidate's code as that user, and a child either one leaves behind --
# `setsid`, a double fork -- would otherwise still be running while root reads
# what the stage wrote. Each round stops them all before killing them, so a
# forking child cannot replace itself between the two. Only this container's /proc is read.
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
    echo "reap: processes of uid $SCORE_UID survived ten rounds" >&2
}

# copy_as_unprivileged <src> <dst>
#
# Read one file the unprivileged user wrote, *as* that user, into a file root
# opens. Root following a path that uid controls is root reading on its behalf:
# a link planted at <src> that points at something only root can read -- the
# sealed recordings -- would otherwise be copied out by root and scored as the
# candidate's own output. Read as nobody, such a link copies nothing. Bounded twice, because <src> may not be a file at all: a
# fifo would block a reader forever, and /dev/zero would fill the disk. 0 and a
# root-owned 0644 <dst> when bytes arrived; 1 and no <dst> otherwise.
COPY_AS_UNPRIVILEGED_MAX=67108864
copy_as_unprivileged() {
    local src=$1 dst=$2
    rm -f "$dst" "$dst.part"
    run_unprivileged test -f "$src" 2>/dev/null || return 1
    if run_unprivileged timeout -k 5 60 head -c "$COPY_AS_UNPRIVILEGED_MAX" -- "$src" \
            > "$dst.part" 2>/dev/null; then
        chmod 0644 "$dst.part"
        mv -f "$dst.part" "$dst"
        return 0
    fi
    rm -f "$dst.part"
    return 1
}

# take_suite_from_unprivileged <dir> <plan>
#
# After the replay and its reap, the transcripts score.py compares and the
# summary it counts sit in <dir>, a directory uid 65534 owns, and candidate code
# runs again as that uid before score.py reads them: the gate's build and pom
# rounds and the repository's own suite. So root moves that directory aside and
# makes <dir> its own, holding only what the plan names -- summary.tsv, one
# <job>.out per planned job and, where the replay wrote one, probe/<job>.tsv --
# each read through copy_as_unprivileged. Nothing the uid does afterwards can
# reach the copies, and nothing it left behind is followed by root to get them.
# The original stays beside it as <dir>.unprivileged, for the evidence.
take_suite_from_unprivileged() {
    local dir=$1 plan=$2 side job n=0 np=0
    side=$dir.unprivileged
    rm -rf "$side"
    mv -T "$dir" "$side" || return 1
    mkdir -p "$dir/probe" || return 1
    chown root:root "$dir" "$dir/probe"
    chmod 0755 "$dir" "$dir/probe"
    copy_as_unprivileged "$side/summary.tsv" "$dir/summary.tsv" || : > "$dir/summary.tsv"
    while IFS="$(printf '\t')" read -r _phase job _rest; do
        [ -n "$job" ] || continue
        case $job in */*|.*) continue ;; esac
        copy_as_unprivileged "$side/$job.out" "$dir/$job.out" && n=$((n + 1))
        copy_as_unprivileged "$side/probe/$job.tsv" "$dir/probe/$job.tsv" && np=$((np + 1))
    done < "$plan"
    echo "$n transcript(s), $np probe file(s) and summary.tsv taken as nobody into root-owned $dir"
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
