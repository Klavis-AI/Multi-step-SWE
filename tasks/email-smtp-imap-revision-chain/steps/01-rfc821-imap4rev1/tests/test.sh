#!/usr/bin/env bash
set -u
source /tests/helpers.sh

# Before any candidate code runs: reward dir and answer key root-only (with a
# pre-written 0), /app handed to the unprivileged uid that run_suite.py runs
# every candidate process as, and a root-only scratch dir for the scorer's
# intermediates. Nothing below runs with cwd /app or a writable sys.path.
harden_reward_dir
seal_tests
give_app_to_unprivileged
W=$(mktemp -d /root/mailscore.XXXXXX 2>/dev/null) || { W=/root/mailscore.$$; mkdir -p "$W"; }
chmod 700 "$W"
cd /

# Always emit reward.json, even when the build or wrap check fails.
# Bounded well inside the 1800 s verifier budget: run_suite.py starts no case
# after 1240 s and caps each at 120 s; this outer limit is the backstop.
timeout -k 15 1400 python3 -I /tests/adapter/run_suite.py all \
    /tests/fixtures/m01 \
    /tests/fixtures/heldout \
    > "$W/suite1.json" 2> "$W/suite1.log" || true
sweep_unprivileged
cp "$W/suite1.log" /logs/verifier/suite1.log 2>/dev/null || true

timeout -k 5 60 python3 -I - "$W" <<'PY' || true
import json, sys
w = sys.argv[1]
p = w + "/suite1.json"
try:
    r = json.load(open(p))
except Exception:
    r = {"build": False, "liveness": False, "wrap_ok": False, "pass": 0, "total": 1}
open("/logs/verifier/suite1.json", "w").write(json.dumps(r, indent=2))
tot = max(int(r.get("total") or 0), 1)
pas = int(r.get("pass") or 0)
# Hard zeros: a tree that did not build, listeners that never answered, or a
# stack caught wrapping a stock MTA/IMAP has earned nothing, so the step scores
# 0. This is not an early stop: the run continues and Harbor still averages
# every step. The named critical cases stay ordinary scored cases.
ok = bool(r.get("build")) and bool(r.get("liveness")) and bool(r.get("wrap_ok"))
# Refusal cases (recorded SMTP 4xx/5xx or IMAP NO/BAD) bound the score: below
# half of them passing, the fraction is scaled by twice the share that pass. A
# server that accepts every command passes none, so it earns 0 instead of the
# accept-path cases (stated in instruction.md).
rt = int(r.get("refusal_total") or 0)
rp = int(r.get("refusal_pass") or 0)
disc = min(1.0, 2.0 * rp / rt) if rt > 0 else 1.0
score = (pas / tot) * disc if ok else 0.0
open(w + "/m1_score", "w").write("%.6f" % score)
PY

# This step's own milestone score in [0,1]; `report` turns it into the binary
# step reward that Harbor averages across steps.
score=$(cat "$W/m1_score" 2>/dev/null || echo 0)
report "$score"
