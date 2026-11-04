#!/usr/bin/env bash
# Entry point for this step's verifier. Everything is driven from Python; this
# wrapper only guarantees that a reward is always written and the exit code is 0.
set -u
source /tests/helpers.sh

# The whole body is one function, defined before any of it runs.
#
# bash reads a script INCREMENTALLY as it executes, so a process that rewrites
# this file mid-run could change what bash does next -- for example, append a
# reward-writing tail and keep this script from ever reaching its own final
# write. Defining a function forces bash to parse to the closing brace before
# executing anything, so the version that runs is the version that was on disk
# when the script started.
_verify() {

# Nothing below runs from a directory the candidate owns.
cd / || true

# Harbor leaves /logs/verifier world-writable. Take it back before the service
# starts; the service runs unprivileged (harness/runner.py), so neither it nor
# anything it leaves running can replace reward.json (helpers.sh).
harden_reward_dir
mkdir -p /scratch
give_app_to_service

# The scorer's diagnostics go to stderr and stream into the log; its
# STDOUT carries one line, the authoritative reward, which is captured here into
# a shell variable. Nothing the candidate does can reach that variable.
#
# The whole scorer is bounded inside the step's verifier timeout (3300 s), which
# Harbor enforces by discarding the step -- and aborting every later one --
# even when a reward.json exists. Its own budget (step.json, 2400 s) is what
# normally ends it; this outer bound is 2980 s + 30 s of grace, and the kill
# loop and sweep below add at most ~40 s, so the script ends by ~3050 s.
# -I: the scorer's imports come from the path it sets itself, never from the
# environment, the user site or the working directory.
out=$(timeout -k 30 2980 python3 -I /tests/harness/score.py /tests/harness/step.json)
rc=$?
reward=$(printf '%s\n' "$out" | sed -n 's/^HARNESS_REWARD \([0-9.]*\)$/\1/p' | tail -1)

# Pre-write the authoritative reward and reopen the directory BEFORE the
# service-uid sweep. If the sweep kills this shell, the reward and the
# host-readable mode must already be on disk; the block below re-writes the
# same value on the normal path. The value is the one the scorer captured in
# the shell variable, so no candidate process can substitute it.
if [ -n "$reward" ]; then
    _pre=$(awk -v r="$reward" 'BEGIN { v = r + 0; if (v < 0) v = 0; if (v > 1) v = 1; if (v == 1) printf "1.000000"; else printf "0.000000" }')
    printf '{"reward": %s}\n' "$_pre" > /logs/verifier/reward.json 2>/dev/null || true
fi
chmod 0777 /logs/verifier 2>/dev/null || true
chmod 0644 /logs/verifier/reward.json /logs/verifier/report.json /logs/verifier/report.txt 2>/dev/null || true

# Every candidate process is killed BEFORE the reward is written, so a detached
# child that outlives the scorer cannot overwrite the real score with its own.
# Kill anything that is not this script or one of its ancestors, twice, and
# only then write the number.
#
# The ancestor chain is walked in full rather than stopping at $PPID: this
# script may run several levels below whatever the harness uses as its
# entrypoint, and killing an intermediate wrapper would break the harness's own
# collection of the result -- turning a hardening measure into a lost run.
ancestors=" 1 $$ "
p=$$
while [ "$p" != "1" ] && [ -r "/proc/$p/stat" ]; do
    p=$(awk '{print $4}' "/proc/$p/stat" 2>/dev/null)
    [ -n "$p" ] || break
    ancestors="$ancestors$p "
done
for pass in 1 2; do
    for pid in $(ls -1 /proc 2>/dev/null | grep -E '^[0-9]+$'); do
        case "$ancestors" in *" $pid "*) continue ;; esac
        kill -9 "$pid" 2>/dev/null
    done
done
# ...and then everything of the service's uid, until none is left: a chain that
# respawns between two /proc snapshots does not survive this loop.
sweep_service_uid

if [ -n "$reward" ]; then
    # Binary step reward: 1 only when the scorer awarded exactly 1.0.
    reward=$(awk -v r="$reward" 'BEGIN { v = r + 0; if (v < 0) v = 0; if (v > 1) v = 1; printf "%.6f", v }')
    if [ "$reward" != "1.000000" ]; then reward="0.000000"; fi
    printf '{"reward": %s}\n' "$reward" > /logs/verifier/reward.json
elif [ ! -s /logs/verifier/reward.json ]; then
    # Written inline rather than through a sourced helper: a helper that is
    # missing or fails to parse leaves the function undefined, the call is
    # "command not found", and this script exits 0 having written NOTHING --
    # which is worse than a zero, because there is no signal at all. The
    # fallback for a scorer that produced no reward must not itself depend on
    # anything that can be absent.
    printf 'verifier: scorer exited %s without writing a reward\n' "$rc" >&2
    printf '{"reward":0.000000}\n' > /logs/verifier/reward.json
else
    # A reward file exists but the scorer never announced a value on stdout:
    # that file's provenance is unknown, so it is not trusted.
    printf 'verifier: no HARNESS_REWARD from the scorer (exit %s); ignoring the existing reward file\n' "$rc" >&2
    printf '{"reward":0.000000}\n' > /logs/verifier/reward.json
fi
release_reward_dir
exit 0

}
_verify "$@"
