#!/usr/bin/env bash
# Step verifier.  Grades a snapshot of /app; nothing written here reaches the
# next step.  Always writes /logs/verifier/reward.json and exits 0.
set -u
# /app is the candidate's tree and the image's WORKDIR: never run Python from
# it.  `python3 -` and `python3 -c` put the cwd on sys.path, so /app/json.py
# would run as root inside the report step below.  Every interpreter here is
# also started with -I.
cd /

source /tests/helpers.sh

# GKE creates /logs/verifier world-writable (0777 root:root).  The verifier runs
# candidate code, so a candidate could write its own reward.json and win by
# last-writer.  Lock it to root BEFORE any candidate code runs; the candidate
# runs as an unprivileged uid (score.py), so the filesystem enforces this.
harden_reward_dir
LOGMODE=$(stat -c '%a' /logs/verifier 2>/dev/null || echo "?")
case "$LOGMODE" in
  700) ;;
  *) echo "WARNING: /logs/verifier is $LOGMODE and could not be tightened" >&2 ;;
esac

# Nothing the candidate could have written may survive into the grading
# result.  Clear the directory before scoring; the scorer writes its result
# last and report() writes reward.json last of all.
rm -f /logs/verifier/*.json

# A provisional zero, written by the verifier itself immediately after the clear.
# If the container is killed mid-scoring, Harbor finds a reward file and records 0
# for this step instead of having nothing for it; report() overwrites it with the
# real score.  This is the verifier's own byte, not the candidate's.
printf '{"reward": 0.0}\n' > /logs/verifier/reward.json

# Set-id bits on the restored tree: the copy is restored with modes preserved,
# and nothing the candidate shipped may run with another identity.  score.py
# also sets no_new_privs on every candidate process.  Before any candidate code
# runs.
find -P /app -perm /6000 -type f -exec chmod ug-s {} + 2>/dev/null || true

# The recorded answers must be unreadable by the candidate's uid before the
# first line of candidate code runs; refuse to score if the filesystem says
# otherwise.
lockdown_tests
if ! lockdown_verify 2>>/logs/verifier/lockdown.log; then
    cat /logs/verifier/lockdown.log >&2
    echo "the recorded answers are readable by the candidate uid - refusing to score"
    report 0
fi

# Whole-scorer bound: Harbor DISCARDS a step whose verifier outlives
# timeout_sec (3600) and aborts every later step.  score.py stops starting work
# at SCORE_DEADLINE (3000 s) and caps every candidate call at 3150 s; this
# outer bound is the backstop, and its expiry path still reports (0).
timeout -k 15 3300 python3 -I /tests/score.py >/logs/verifier/score.log 2>&1
rc=$?
sweep_candidate

# A report is written on EVERY path, including the failure path, so that a
# legitimate zero and a container that died are distinguishable afterwards.
python3 -I - "$rc" <<'PY'
import json, os, sys
rc = int(sys.argv[1])
have = os.path.exists("/logs/verifier/score.json")
rep = {"scorer_exit_code": rc, "score_json_written": have,
       "tail": open("/logs/verifier/score.log").read()[-2000:] if os.path.exists("/logs/verifier/score.log") else ""}
if have:
    d = json.load(open("/logs/verifier/score.json"))
    rep["reward"] = d.get("reward")
    rep["groups"] = {k: v.get("score") for k, v in (d.get("groups") or {}).items()}
    rep["missing_tools"] = d.get("missing_tools")
    rep["elapsed_sec"] = d.get("elapsed_sec")
else:
    rep["reward"] = None
json.dump(rep, open("/logs/verifier/report.json", "w"), indent=1)
PY

tail -40 /logs/verifier/score.log || true
if [ $rc -ne 0 ] || [ ! -f /logs/verifier/score.json ]; then
    echo "scorer failed (rc=$rc); reporting 0 — see /logs/verifier/report.json"
    report 0
fi
report "$(python3 -I -c "import json;print(json.load(open('/logs/verifier/score.json'))['reward'])")"
