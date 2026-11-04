#!/usr/bin/env bash
# The whole verifier for one step. Kept in sync by authoring/sync_tests.py;
# edit authoring/common/tests/test.sh, never a copy.
#
# Every step runs the same program. What differs is /tests/plan.py, which names
# this step's families and their weights; see wglib/score.py for the contract.
set -u
source /tests/helpers.sh

# First, before anything the candidate's tree could reach.
harden_reward_dir
strip_restored_setid || {
    echo "test.sh: a set-id file survived the strip" >> /logs/verifier/verifier.log
    report 0
}

export PATH="/usr/lib/postgresql/17/bin:${PATH}"
export LC_ALL=C.UTF-8
export LANG=C.UTF-8

LOG=/logs/verifier/verifier.log

# The scorer runs as root, so where its imports come from is part of the
# scoring contract. Harbor starts this script with cwd /app -- the candidate's
# own tree -- and `python3 -m` puts the cwd first on sys.path, so an
# /app/wglib/ (or any top-level module the scorer imports) used to run in place
# of the scorer, as root (reward-hacking audit 2026-10-12, F1). So: cwd is /,
# the interpreter is isolated (-I: no cwd, no PYTHON* variables, no user site),
# and /tests goes onto sys.path explicitly. -I also ignores
# PYTHONPYCACHEPREFIX, so the bytecode cache is named on the command line --
# trap 8, .pyc files must not land in /tests -- and it is a fresh root-only
# directory, because a cache directory one of the candidate's accounts could
# create first is another way to hand the scorer a module.
cd /
SCRATCH="$(mktemp -d /tmp/wgscore.XXXXXX)" || SCRATCH=/tmp
SCORER='import runpy, sys
sys.path.insert(0, "/tests")
runpy.run_module("wglib.score", run_name="__main__", alter_sys=True)'

# One wall clock for the whole scorer (audit F3). Every wait inside it carries
# its own bound, but nothing bounded their sum, and a query against the
# candidate's own cluster is candidate code. Harbor discards a step whose
# verifier outlives `[steps.verifier] timeout_sec` even when reward.json
# exists, and aborts every later step, so the bound has to sit inside the
# smallest of those, less 240 s: 3600 (steps 01-02; 5400 for 03-06) - 240 =
# 3360 for the whole script, and the scorer's share is 3300 + 30 s kill grace
# + the sweep (at most ten passes) + report, about 3345.
SCORE_DEADLINE=3300

# score.py writes /logs/verifier/report.json itself, always, and prints one
# line. It is built never to raise and never to exit non-zero; if it manages it
# anyway, or the deadline kills it, the line is missing and report() scores
# this step 0 rather than crashing the trial and taking the remaining steps out
# of the mean with it. The line goes to a root-only file, not a pipe: a
# command substitution waits for every holder of its pipe, and a daemon the
# scorer started could hold it past the deadline.
timeout -k 30 "$SCORE_DEADLINE" \
    python3 -I -X pycache_prefix="$SCRATCH/pyc" -c "$SCORER" /tests/plan.py \
    > "$SCRATCH/score" 2>"$LOG"
rc=$?
[ "$rc" = 124 ] || [ "$rc" = 137 ] && \
    echo "test.sh: the scorer passed its ${SCORE_DEADLINE}s deadline (rc=$rc)" >> "$LOG"

# Nothing the candidate's code started may outlive the scorer into report().
sweep_candidate_uids

report "$(head -n 1 "$SCRATCH/score" 2>/dev/null)"
