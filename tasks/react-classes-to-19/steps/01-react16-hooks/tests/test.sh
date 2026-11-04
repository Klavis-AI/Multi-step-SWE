#!/usr/bin/env bash
set -u
source /tests/helpers.sh

# Lock the reward directory before anything else.  Everything that executes
# candidate code - the dependency install, the build, the server, the browser -
# runs through run_unprivileged, so candidate code cannot write its own score.
harden_reward_dir

# The runner is sourced before the log redirect rather than after it, because
# the step number comes out of it.  Sourcing only defines functions, so there is
# no output here for the logs to miss.
source /tests/runner/run.sh

# Which step this image is for, taken from the gate spec it ships rather than
# written here.  That makes this file byte-identical in every step's tests/
# directory, which is the point: see spec_step() in runner/run.sh for the build
# defect a per-step copy of this file was silently exposed to.
STEP="$(spec_step)"
if [ -z "$STEP" ]; then
    echo "test: /tests/spec.json does not say which step this is" \
        >/logs/verifier/step-unknown-stderr.log 2>/dev/null
    report 0
fi

# One bound on the whole run.  Harbor discards a verifier that outlives
# [steps.verifier] timeout_sec (2700 s at every step) even when reward.json is
# already written, and then aborts every later step, so no phase the candidate
# can make hang may reach that limit.  This file runs itself once more under a
# root-owned `timeout`; when the bound fires, every process the run left is
# killed and the step scores 0.  2400 s plus the 30 s kill grace and the sweep
# stays 240 s under the timeout.  (wave A 2026-10-12, reward-hacking review F2)
if [ "${1:-}" != "--bounded" ]; then
    timeout -k 30 2400 bash /tests/test.sh --bounded
    bounded_rc=$?
    reap_uid "$SCORE_UID"
    reap_uid "$SERVER_UID"
    if [ "$bounded_rc" -eq 124 ] || [ "$bounded_rc" -eq 137 ] || [ ! -f /logs/verifier/reward.json ]; then
        echo "test: the verifier did not finish inside its 2400 s budget, or wrote no reward (exit $bounded_rc); scoring 0" \
            >>"/logs/verifier/step${STEP}-stderr.log" 2>/dev/null
        rm -f /logs/verifier/reward.json
        report 0
    fi
    exit 0
fi

# Named for this step, by this step.  If a multi-step run reuses one verifier
# image across steps, the logs in this directory carry another step's number and
# the mismatch is visible instead of being averaged into the mean.  Plain file
# redirection rather than a tee process substitution: the shell flushes these on
# exit, and `report` exits.
exec >"/logs/verifier/step${STEP}-stdout.log" 2>"/logs/verifier/step${STEP}-stderr.log"

run_step "$STEP"

report "$STEP_REWARD"
