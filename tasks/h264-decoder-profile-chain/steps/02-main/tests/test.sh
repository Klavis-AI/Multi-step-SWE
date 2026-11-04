#!/usr/bin/env bash
# Runs in this step's verifier container on a fresh copy of /app. Nothing
# written here reaches the next step; only /logs/verifier is read back.
set -u
source /tests/helpers.sh
mkdir -p /logs/verifier

# NO EXTERNAL BINARIES ON THE POST-DECODE PATH. The candidate's decoder runs in
# this container unprivileged (uid 65533) while the scorer is root, so any
# program invoked AFTER the decode is a program it
# can replace first. Extracting the reward with `sed` and `tail`, say, would let
# three lines written over /usr/bin/sed from inside the decoder forge the value,
# and no workspace scan could see it, because the attack is not in the
# workspace. `printf`, `[[ =~ ]]` and `read` below are bash builtins,
# resolved inside this already-running shell.
#
# `node` and `tee` are exempt by construction: bash forks both at pipeline
# start, before a single line of candidate code executes, so replacing those
# files on disk afterwards cannot affect the processes already running.

# A breadcrumb written before any decoding starts. If the container is killed
# outright -- cgroup OOM, kubelet eviction, SIGKILL -- this is the difference
# between "the verifier never ran" and "the verifier ran and died", which a
# 0-byte stdout cannot distinguish.
printf -v _started '%(%Y-%m-%dT%H:%M:%SZ)T' -1
printf 'verifier started %s\n' "$_started" > /logs/verifier/started_at
rm -f /logs/verifier/reward.json /logs/verifier/report.json
# Harbor runs `chmod 777 /logs/verifier` (empty_dirs, chmod=True) just before
# this script, so the "0755 root:root" below holds in the bare image but not
# under Harbor. Take the directory back to root 0700
# BEFORE any decode runs, so the decode uid cannot create, replace or occupy
# any entry in it. `report` reopens it to 0755 (still root-owned) at the end.
# Pre-decode, so these external binaries are not on the post-decode path.
chown root:root /logs/verifier 2>/dev/null || true
chmod 700 /logs/verifier 2>/dev/null || true

# The reward must not be taken from a file the candidate could reach. Measured
# in the shipped image: THREE places create /logs/verifier and all three run as
# root, each leaving it 0755 root:root: `RUN mkdir -p` in the Dockerfile, the
# `mkdir -p` above, and score.mjs's `mkdirSync(LOGDIR, {recursive:true})` in
# main() -- measured both when it pre-exists and when it does not,
# since recursive mkdir does not widen a directory it creates fresh. The decode
# is spawned as uid 65533, which cannot write there in any of those states.
# Nothing at runtime widens it: the scorer only chmods SCRATCH (/tmp/h264-score)
# to 0711, and openUp() widens READ access under /app alone.
#
# The defence below is defence in depth for a file the ROOT scorer does write,
# and against any future change that relaxes those modes. Two requirements pull
# in opposite directions and both are met here:
#
#   * the authoritative value is the scorer's stdout captured into a shell
#     VARIABLE -- no file, nothing a candidate process can rewrite;
#   * `tee` writes the same stream to scorer.log incrementally, so a scorer
#     that is killed still leaves its partial output on disk for diagnosis.
#
# scorer.log is diagnostics only. It is never parsed for the reward: doing so
# would make a file, rather than the captured stdout, the reward source.
# The scorer is bounded below the step's verifier timeout
# (3600 s). The workspace scan reads every path the walk yields -- a hard link
# or an in-tree symlink is a path of its own -- and runs before the case
# deadline is set, so links planted in /app could otherwise carry the run past
# Harbor's limit: no reward.json, and every later step aborted. The worst case
# for a well-formed workspace is deadline_sec + case_timeout_sec + the scan
# (about 3120 s at step 4).
# `timeout` is forked with `node`, before any candidate code runs, so it is
# not on the post-decode path either. On expiry no REWARD= line exists and the
# step reports 0 below.
# No core dumps from the scorer or any decode it spawns. A
# crashing decode would otherwise write a multi-gigabyte core into its scratch
# directory and could fill the disk under the cases that follow. `ulimit` is a
# builtin and sets the hard limit too, so the decode uid cannot raise it.
ulimit -c 0
out=$(timeout -s KILL 3300 node /tests/score.mjs 2>&1 | tee /logs/verifier/scorer.log)
rc=${PIPESTATUS[0]}

# Reap the decode uid before the reward is written. The
# scorer kills each decode's process group, but a scorer stopped by the bound
# above leaves its in-flight decodes running. Builtins only (read, kill, case):
# nothing here is a program the decode uid could have replaced.
_i=0
while [ "$_i" -lt 10 ]; do
    _i=$((_i + 1)); _left=0
    for _s in /proc/[0-9]*/status; do
        _st=""; _uid=""
        while read -r _k _v _rest; do
            case "$_k" in State:) _st=$_v ;; Uid:) _uid=$_v; break ;; esac
        done 2>/dev/null < "$_s"
        if [ "$_uid" = "65533" ] && [ "$_st" != "Z" ] && [ "$_st" != "X" ]; then
            _p=${_s#/proc/}; _p=${_p%/status}
            kill -KILL "$_p" 2>/dev/null; _left=1
        fi
    done
    [ "$_left" = "0" ] && break
done
printf '%s\n' "$out"

if [ "$rc" -ne 0 ]; then
    echo "scorer exited $rc; reporting 0"
    report 0
fi

# Extract REWARD= with builtins only: no sed, no tail.
truth=""
while IFS= read -r _line; do
    if [[ "$_line" =~ ^REWARD=([0-9]+(\.[0-9]+)?)$ ]]; then
        truth="${BASH_REMATCH[1]}"
    fi
done <<< "$out"

if [ -z "$truth" ]; then
    echo "scorer printed no REWARD= line; reporting 0"
    report 0
fi

# Last action, after the scorer (which kills each decode's whole process group)
# has returned.
report "$truth"
