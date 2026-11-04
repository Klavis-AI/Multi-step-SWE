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
timeout -k 15 1400 python3 -I /tests/adapter/run_suite.py all /tests/fixtures/m01 /tests/fixtures/m02 /tests/fixtures/heldout \
    > "$W/suite.json" 2> "$W/suite.log" || true
sweep_unprivileged
cp "$W/suite.log" /logs/verifier/suite.log 2>/dev/null || true

timeout -k 5 60 python3 -I - "$W" <<'PY' || true
import json, sys
w = sys.argv[1]
p = w + "/suite.json"
try:
    r = json.load(open(p))
except Exception:
    r = {"build": False, "liveness": False, "wrap_ok": False, "pass": 0, "total": 1}
open("/logs/verifier/suite.json", "w").write(json.dumps(r, indent=2))
# Product scoring: T1 is this step's own fixture group, T2 every
# earlier group plus the held-out set. Each is the plain fraction of its cases
# that pass, and the step scores T1 x T2, so a tree that keeps the earlier
# behaviour but does none of this step's work earns T2 and still scores 0.
# A case of this step's own group that repeats an earlier group's case (or a
# held-out case) unchanged -- the same check, by run_suite.py's case_key -- is
# earlier work and is counted in T2, not T1.
OWN = ("m02",)
REST = ("m01", "heldout")
# An own-group case that re-checks earlier behaviour this revision leaves
# unchanged (different bytes, same earlier requirement) is earlier work too
# and is counted in T2. Each was passed by the
# previous step's reference tree under this verifier, measured per case, and
# checks nothing this step's instruction lists as new; a case that does stays
# in T1 even when that reference passes it. Listed by run_suite.py case_key.
INHERITED = frozenset((
    "6ce6cdf262d4c3c13b6d5b96b37eaba2c70063afe691a427c9c8753e739f3aeb",  # smtp.helo.still
))
rows = [c for c in (r.get("cases") or []) if isinstance(c, list) and len(c) == 3]
earlier = {k for g, k, _ in rows if g in REST} | INHERITED
def frac(sel):
    got = [bool(ok) for g, k, ok in rows if sel(g, k)]
    return (sum(got) / len(got) if got else 0.0), len(got)
t1, n1 = frac(lambda g, k: g in OWN and k not in earlier)
t2, n2 = frac(lambda g, k: g in REST or (g in OWN and k in earlier))
# Hard zeros: a tree that did not build, listeners that never answered, or a
# stack caught wrapping a stock MTA/IMAP has earned nothing, so the step scores
# 0. This is not an early stop: the run continues and Harbor still averages
# every step. The named critical cases stay ordinary scored cases.
ok = bool(r.get("build")) and bool(r.get("liveness")) and bool(r.get("wrap_ok"))
# Refusal cases (recorded SMTP 4xx/5xx or IMAP NO/BAD) bound the score: below
# half of them passing, the product is scaled by twice the share that pass. A
# server that accepts every command passes none, so it earns 0 instead of the
# accept-path cases (stated in instruction.md).
rt = int(r.get("refusal_total") or 0)
rp = int(r.get("refusal_pass") or 0)
disc = min(1.0, 2.0 * rp / rt) if rt > 0 else 1.0
score = t1 * t2 * disc if ok else 0.0
open("/logs/verifier/score_detail.json", "w").write(json.dumps(
    {"ok": ok, "t1": t1, "t1_cases": n1, "t2": t2, "t2_cases": n2,
     "refusal_bound": disc, "score": score}))
open(w + "/score", "w").write("%.6f" % score)
PY

# This step's own milestone score in [0,1]; `report` turns it into the binary
# step reward that Harbor averages across steps.
score=$(cat "$W/score" 2>/dev/null || echo 0)
report "$score"
