#!/usr/bin/env bash
# Every step's verifier entry point. Identical in all seven; the step's
# identity, interpreter and weights arrive in /tests/step.json, written by
# build/sync.sh. Do not edit the copy under steps/*/tests -- edit this one and
# re-run build/sync.sh.
set -u
source /tests/helpers.sh

# First, before anything else runs: candidate code is about to be imported in
# this container, and a detached child of it outlives the scorer.
harden_reward_dir

# The whole run is held under the step's [steps.verifier] timeout_sec (1800 s
# on every rung). Harbor discards a step whose verifier overruns -- even one
# that already wrote reward.json -- and aborts every step after it, so an
# overrun must not happen. Everything up to the scorer's start counts against
# TOTAL_BUDGET; the scorer gets what is left (never less than 60 s) and is
# killed when that runs out, and the step then scores what it printed, which is
# 0 if it printed nothing. Worst case: max(TOTAL_BUDGET 1500, set-up 3 x 130 +
# 60) + 30 kill grace + sweep and report ~10 = 1540 s, inside 1800 - 240.
# Inside the scorer the replay stops starting cases GATE_RESERVE (score.py)
# before the deadline.
TOTAL_BUDGET=1500
t0=$(date +%s)

# Nothing below runs from /app. The scorer is root and must not have the
# candidate's tree on its import path, and the cwd Harbor hands this script is
# /app.
cd /

# /app is repopulated from the collected artifact with whatever ownership the
# writer had. It becomes root's, and read-only to everyone else, before any
# candidate code runs: the scorer copies it twice (one copy the cases run, one
# the gates read), and both copies have to be of the tree as it was handed in.
timeout -k 10 120 chown -R root:root /app 2>/dev/null || true
timeout -k 10 120 chmod -R a+rX,go-w /app 2>/dev/null || true

# The scorer's own files: the gate tree, its report and its stdout. 0700 root,
# so nothing the candidate's uid runs can open, list or replace them.
SROOT=/tmp/score-root
rm -rf "$SROOT"
mkdir -m 700 "$SROOT"
# The one bytecode-cache prefix every process below inherits. Root-made and
# empty, inside the 0700 dir above: a root process (the replay driver, a
# checker run as a console script) that honours the variable can read no
# .pyc from it, and uid 65534 can neither enter it nor plant one there. A
# prefix under /tmp (1777) was one the candidate uid could create first
# (wave-D S-C1).
mkdir -m 700 "$SROOT/pyc"

# One TLS fixture for the whole run rather than one per case: certs.py honours
# an inherited FIXTURE_CERTS. Built here, by root, before any candidate code
# exists in this container, so the cases can read it and nothing they run can
# rewrite it.
export FIXTURE_CERTS=/tmp/fxcerts
rm -rf "$FIXTURE_CERTS"
mkdir -m 755 "$FIXTURE_CERTS"
timeout -k 10 120 "$VERIFIER_PYTHON" -I -B -c \
    'import sys; sys.path.insert(0, "/tests"); from harness import certs; certs.build(sys.argv[1])' \
    "$FIXTURE_CERTS" >/dev/null 2>&1 \
    || echo "test.sh: WARNING the TLS fixture did not build"
chmod -R a+rX "$FIXTURE_CERTS" 2>/dev/null || true

OUT="$SROOT/out"
deadline=$(( t0 + TOTAL_BUDGET ))
budget=$(( deadline - $(date +%s) ))
[ "$budget" -ge 60 ] || budget=60
# The scorer runs as root in isolated mode (-I: no cwd, no PYTHON* variables
# and no user site on its own path; /tests is put there explicitly) and drops
# every process that runs the tree -- the import probe and each case -- to uid
# 65534. The variables below are for those children. PYTHONDONTWRITEBYTECODE,
# so the copies the scorer makes stay pristine. TZ/PYTHONHASHSEED to match the
# recording's seed.
timeout -k 30 "$budget" env \
    PYTHONPATH=/tests \
    PYTHONDONTWRITEBYTECODE=1 \
    PYTHONPYCACHEPREFIX="$SROOT/pyc" \
    FIXTURE_CERTS="$FIXTURE_CERTS" \
    TZ=UTC \
    PYTHONHASHSEED=0 \
    "$VERIFIER_PYTHON" -I -B -c \
        'import runpy, sys; sys.path.insert(0, "/tests"); runpy.run_module("harness.score", run_name="__main__", alter_sys=True)' \
        --config /tests/step.json \
        --tree /app \
        --report "$SROOT/score.json" \
        --scratch /tmp/pristine \
        --gate-tree "$SROOT/tree" \
        --deadline "$deadline" \
    >"$OUT" 2>&1
rc=$?
if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
    echo "test.sh: the scorer was stopped at its ${budget}s budget" >>"$OUT"
fi
reap_unprivileged
cat "$OUT"

# Copied out as root while /logs/verifier is still 700. These are the artifacts
# that make a score diagnosable: which cases failed and how, which gates fired
# and where, and whether the run was blocked rather than bad.
cp -f "$SROOT/score.json"        /logs/verifier/score.json        2>/dev/null || true
cp -f "$SROOT/score.json.replay" /logs/verifier/replay.json       2>/dev/null || true
cp -f "$OUT"                     /logs/verifier/score.log         2>/dev/null || true

# No REWARD line means the scorer died before it could produce one -- a crash,
# an OOM, a tree that cannot be imported at all, or the budget above. That is a
# zero, and score.log says why. Never a non-zero exit: Harbor treats that as a
# verifier crash and skips the remaining steps.
reward=$(awk '/^REWARD /{v=$2} END {print (v == "" ? 0 : v)}' "$OUT")
report "$reward"
