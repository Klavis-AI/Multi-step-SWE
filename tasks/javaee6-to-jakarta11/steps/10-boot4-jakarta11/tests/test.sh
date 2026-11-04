#!/usr/bin/env bash
# What Harbor runs in this step's verifier container.
#
# Per-step copy: every step's tests/ holds an identical copy of this file;
# keep them in sync. The part that differs between rungs is score.py -- this
# file is the same four stages everywhere, so that a step cannot accidentally
# score itself by a different route than its neighbours.
#
#   build.sh          the candidate's own build tool, the verifier's toolchain
#   scan.sh           what the artifact and the source actually contain
#   run_behaviour.sh  the recorded suite, replayed through real clients
#   score.py          this step's gates, and reward = migration * behaviour
set -u
source /tests/helpers.sh

# First, before anything that could start a process: Harbor opens
# /logs/verifier to 0777 just before this container starts.
harden_reward_dir
score_setup

bash /tests/build.sh          2>&1 | tee -a /score/log/stage.log >&2
# Nothing the candidate's build started outlives it (helpers.sh says why).
reap_unprivileged
bash /tests/scan.sh           2>&1 | tee -a /score/log/stage.log >&2
# scan.sh runs the candidate's build once more (its dependency listing).
reap_unprivileged
bash /tests/run_behaviour.sh  2>&1 | tee -a /score/log/stage.log >&2
reap_unprivileged

# The reward travels in a variable rather than being read back from a file.
# reward.txt is still written, for the record.
score="$(python3 /tests/score.py 2>>/score/log/stage.log)"
rc=$?
record score.rc "$rc"
printf '%s\n' "$score" > /score/out/reward.txt 2>/dev/null || true

# Copy the summary out for the maintainer. /logs/verifier is collected by the
# harness and never handed to the candidate -- the only thing that reaches the
# next step is /app.
cp -f /score/out/report.json   /logs/verifier/ 2>/dev/null || true
cp -f /score/out/journal.tsv   /logs/verifier/ 2>/dev/null || true
tail -c 200000 /score/log/stage.log > /logs/verifier/stage.log 2>/dev/null || true

# An empty or unparsable score clamps to 0 rather than crashing: a
# verifier that exits non-zero is a step that disappears from the mean, which
# reads as a task that was never attempted instead of one that scored nothing.
report "$score"
