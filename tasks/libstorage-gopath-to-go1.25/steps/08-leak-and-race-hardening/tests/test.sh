#!/usr/bin/env bash
# Score this step. Runs as root; everything that touches the candidate's tree
# runs as nobody, which is also the identity the expectations were recorded
# under. The recorded answers themselves are root-only while anything runs as
# nobody: the candidate's server is one of those processes, so the comparison
# against them is made here, as root, by judge.py.
set -u
# Harbor starts this script with cwd = /app, the candidate's tree. Leave it
# before any interpreter runs: `python3 -c` puts the cwd on sys.path, so a
# planted /app/json.py would otherwise run here, as root.
cd /
# Root's PATH in this image starts with /go/bin, which the golang base image
# creates world-writable (1777): code running as nobody could plant a
# `python3` or `cp` there for the root commands below. Nothing here needs it.
export PATH=/usr/local/go/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
source /tests/helpers.sh

# Which step this verifier image grades, taken from the gate specification so
# that this file stays identical across steps. One verifier image is built per
# step, from that step's tests/ as the build context -- but a local docker
# environment may build only one for the whole task, in which case a later step
# silently runs an earlier step's test.sh and scores well against a reference
# that implements every milestone. The stamp below is what makes that visible
# from outside the container: `ls <job>/*/*/steps/NN-*/verifier` has to mention
# step<NN> and no other number.
STEP_ID=$(python3 -I -c 'import json
print(json.load(open("/tests/lib/gatespec.json"))["step"])' 2>/dev/null || echo unknown)

harden_reward_dir
# The recorded answers. Nothing at the candidate's uid may read them: score.py
# only collects the server's answers, and judge.py (root) compares them.
chown -R root:root /tests/lib/expect
chmod -R go-rwx /tests/lib/expect
# Root's own working directory for everything the reward is computed from.
# score.py writes its breakdown through a descriptor opened here, so no file a
# candidate process can reach is ever read back as a result.
FINAL=/root/lsverify
rm -rf "$FINAL"
mkdir -m 0700 "$FINAL"
printf '%s\n' "$STEP_ID" > "/logs/verifier/step${STEP_ID%%-*}-identity.txt"

# What the gates measure: root's copies of the tree as it
# was handed in and of the image's module cache, taken here before any
# candidate code runs, root-owned and read-only to the candidate's uid. /app
# itself is the candidate's while its server and its tests run, and a process
# they leave behind could rewrite it, or the a+rwX module cache, before a gate
# reads it. A copy that cannot be taken is not evidence either way: report 0.
# The copies are bounded and count against the scorer's deadline below.
SNAPSHOT_STARTED=$SECONDS
GATED=/run/lsgate
rm -rf "$GATED"
mkdir -p -m 0755 "$GATED"
if ! snapshot_tree /app "$GATED/app" \
        || ! snapshot_tree /tmp/gopath/pkg/mod "$GATED/modcache"; then
    echo "test.sh: could not take root's copy of /app or the module cache" >&2
    report 0
fi
give_app_to_unprivileged

# The scorer needs writable scratch for the module cache, the build output and
# the per-scenario server roots.
for d in /tmp/gocache /tmp/gopath /tmp/scratch; do
    mkdir -p "$d"
    chown -R "$SCORE_UID:$SCORE_GID" "$d" 2>/dev/null || true
done

# Kill every process at the candidate's uid, and wait until none is left.
reap_unprivileged() {
    python3 -I - "$SCORE_UID" <<'REAP' 2>/dev/null || true
import os, signal, sys, time
uid = int(sys.argv[1])
for _ in range(50):
    hit = False
    for pid in os.listdir('/proc'):
        if not pid.isdigit():
            continue
        try:
            if os.stat('/proc/' + pid).st_uid == uid:
                os.kill(int(pid), signal.SIGKILL)
                hit = True
        except OSError:
            pass
    if not hit:
        break
    time.sleep(0.2)
REAP
}

# Bound the whole scorer well under [steps.verifier] timeout_sec (3600): score.py
# runs build -> replay -> own-tests -> census -> every gate -> every regression
# item in sequence, and toolenv enforces only a *per-call* timeout, so their sum
# (build 900 + replay 1500 + own 1200 + census 600 + the step's probe caps)
# exceeds 3600 from step 02 on. A candidate init() deadlock in a widely-imported
# package would drive every phase to its cap and run past the budget; Harbor then
# discards the step and aborts the rest, locking in the mean of the earlier ones.
# This outer deadline makes the hang a step-local 0 instead: score.py is killed,
# no /tmp/score.json is written, and the report 0 below runs. 3300 s leaves
# >=300 s of the budget for the /app artifact round trip; the oracle scores in
# under four minutes. The clock starts before root's copies of /app and the
# module cache are taken, so their time comes out of the same 3300 s.
VERIFIER_DEADLINE_SEC=3300
STARTED=$SNAPSHOT_STARTED
SCORE_SEC=$((VERIFIER_DEADLINE_SEC - (SECONDS - STARTED)))
[ "$SCORE_SEC" -ge 60 ] || SCORE_SEC=60

bounded_unprivileged "$SCORE_SEC" 30 env TMPDIR=/tmp/scratch \
    python3 -s -E /tests/lib/score.py 3> "$FINAL/score.json" \
    > "$FINAL/score.log" 2>&1
rc=$?

if [ "$rc" = 124 ] || [ "$rc" = 137 ]; then
    # The deadline fired; report 0 follows.
    echo "verifier deadline ${VERIFIER_DEADLINE_SEC}s reached (rc=$rc)" >&2
fi

# Reap every candidate-uid process before anything the scorer wrote is read, on
# every path: the scorer has exited, nothing at that uid is needed any more, and
# a process the candidate's own code detached (its server, its tests) must not
# outlive the scorer to rewrite the files read below. toolenv starts each go
# process in its own session, so some may also outlive a killed scorer. This
# image has no procps, so read /proc.
reap_unprivileged

# The breakdown is read back with the reward, which is how a failed step is
# diagnosed without re-running it. Only /logs/verifier leaves the container.
# Everything copied here is a root-owned file in $FINAL, and the logs, whose
# length the candidate's code decides, are copied with a bound.
head -c 16777216 "$FINAL/score.log" > /logs/verifier/score.log 2>/dev/null || true

if [ $rc -ne 0 ] || [ ! -s "$FINAL/score.json" ]; then
    # A scorer that could not finish is not evidence that the candidate failed,
    # but it is not evidence of success either. Report zero and leave the log.
    report 0
fi

# The one transport retry: whole scenarios whose answer says the server never
# finished, unless the recording says that is the answer (replay.py,
# `unfinished_retry_ids`). Only root can tell the two apart, so root decides,
# and the re-run is bounded by what is left of the scorer's deadline.
RETRY_IDS=$(python3 -I -B /tests/lib/judge.py retry-ids "$FINAL/score.json" \
    2>> "$FINAL/judge.log" | tail -n 1)
LEFT=$((VERIFIER_DEADLINE_SEC - (SECONDS - STARTED)))
if [ -n "$RETRY_IDS" ] && [ "$LEFT" -gt 30 ]; then
    bounded_unprivileged "$LEFT" 10 env TMPDIR=/tmp/scratch \
        python3 -s -E /tests/lib/score.py --retry "$RETRY_IDS" \
        3> "$FINAL/retry.json" > "$FINAL/retry.log" 2>&1
    reap_unprivileged
fi

# Compare, apply the retry, and compute the reward, as root. judge.py writes
# the reward with the breakdown; a judge that could not finish reports 0.
python3 -I -B /tests/lib/judge.py final "$FINAL/score.json" "$FINAL/retry.json" \
    "$FINAL" >> "$FINAL/judge.log" 2>&1
jrc=$?
cp "$FINAL/breakdown.json" /logs/verifier/breakdown.json 2>/dev/null || true
cp "$FINAL/behavior.json" /logs/verifier/behavior.json 2>/dev/null || true
head -c 16777216 "$FINAL/judge.log" > /logs/verifier/judge.log 2>/dev/null || true
head -c 16777216 "$FINAL/retry.log" > /logs/verifier/retry.log 2>/dev/null || true
if [ "$jrc" -ne 0 ] || [ ! -s "$FINAL/reward" ]; then
    report 0
fi
report "$(head -c 64 "$FINAL/reward")"
