#!/usr/bin/env bash
set -u
source /tests/helpers.sh

# Every mode this verifier creates is set by the package rather than
# inherited: at umask 0077 a file the verifier writes for the confined
# candidate to read lands 0600 owned by root, and the step reports a broken
# candidate whose real fault is the machine the image happened to run on.
umask 0022

# Runs in this step's verifier container on a fresh copy of /app.  Everything is
# re-derived here: the candidate is rebuilt from source, started by the verifier,
# and driven only through the FHIR RESTful API.
mkdir -p /logs/verifier
# A provisional reward, so that a verifier the kernel or the cluster kills
# outright reports this step as 0 rather than failing the step and aborting
# every step after it.  The real score overwrites it below.
printf '{"reward":0.000000}\n' > /logs/verifier/reward.json
harden_reward_dir

# The confinement below runs as an unprivileged uid.  Whatever modes the
# restored /app arrived with, that uid has to be able to read its own sources
# and run its own binary, or the guard would report a broken candidate when the
# real fault is the transfer.  Widening read access to the candidate's own tree
# costs nothing -- it is the candidate's tree.
chmod a+rX /app 2>/dev/null
timeout -k 5 60 chmod -R a+rX /app/src 2>/dev/null

# Harbor discards a step whose verifier outlives [steps.verifier] timeout_sec
# (3,600 s) even when reward.json exists, and then aborts every later step, so a
# server that can make the scorer hang keeps the mean of the steps before it.
# The scorer budgets itself to finish inside 3,150 s (harness.budget_left); this
# bounds whatever escapes that budget.  Worst case: 65 s (the chmod above) +
# 3,250 s + 20 s kill grace + the sweep, under 3,600 - 240.  A scorer that is
# killed reports nothing, and this step scores 0.
#
# Run from /, isolated from the environment, with its output in a directory
# only root can write.  The score is taken only from a scorer that exited
# normally, and only from the last SCORE= line it printed: the build's output is
# echoed into this log earlier, so a line there is never the last one -- unless
# the scorer is killed before it prints its own, which is why a killed scorer
# reports nothing.
W=$(mktemp -d)
( cd / && timeout -k 20 3250 python3 -I /tests/lib/score.py ) > "$W/score.log" 2>&1
src=$?
cp "$W/score.log" /logs/verifier/score.log
cat "$W/score.log"
SCORE=""
if [ "$src" -eq 0 ]; then
    SCORE=$(grep -E '^SCORE=[0-9.]+$' "$W/score.log" | tail -n 1 | cut -d= -f2)
else
    echo "scorer did not finish (exit $src); this step scores 0"
fi

# A kill sweep before the reward is written: a process the candidate left
# running is still a process racing this script, and the reward is the one
# number in the container that must be the verifier's.  report() sweeps
# everything the candidate's uid owns, until nothing is left, then writes.
[ -z "${SCORE:-}" ] && SCORE=0
report "$SCORE"
