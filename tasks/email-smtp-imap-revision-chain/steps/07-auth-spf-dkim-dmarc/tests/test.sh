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
timeout -k 15 1400 python3 -I /tests/adapter/run_suite.py all /tests/fixtures/m01 /tests/fixtures/m02 /tests/fixtures/m03 /tests/fixtures/m04 /tests/fixtures/m05 /tests/fixtures/m06 /tests/fixtures/m07 /tests/fixtures/heldout \
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
OWN = ("m07",)
REST = ("m01", "m02", "m03", "m04", "m05", "m06", "heldout")
# An own-group case that re-checks earlier behaviour this revision leaves
# unchanged (different bytes, same earlier requirement) is earlier work too
# and is counted in T2. Each was passed by the
# previous step's reference tree under this verifier, measured per case, and
# checks nothing this step's instruction lists as new; a case that does stays
# in T1 even when that reference passes it. Listed by run_suite.py case_key.
INHERITED = frozenset((
    "e747a67abfff972730e76f2ee6be3c810217b613681c6f93850103fa489ec376",  # smtp.ehlo.auth
    "88ea9c95c1312cd6a52317aa475917c6c55255cc2f77a4df4073395f329a9452",  # smtp.expn.still.500
    "da3ec0d0b6ea03a235ae6f2f9d7d2981634c631972a6ae165b1e5ece8ba5cf69",  # smtp.source.route.still.501
    "d55c330fbf50291e688213f94b489df5b2c1aceffd5ed564bd199c556b849449",  # smtp.smtputf8.still
    "4b83ee8664255c6cc66d1c0083d67f236c7e74802c1cc876a3a7e3545a47e355",  # smtp.auth.then.mail
    "1b2b1637f4df12681ce80662a8a9ed15a046ed96f1b7e98c211faad017535c0d",  # smtp.data.no.rcpt.still.554
    "440184e607405904b448d8a9460f7ff7621bf2fae0a614f49e6d4c3fbd8863b6",  # smtp.rcpt.unknown.still.550
    "b4bca87256597c360b851739a733bfcb35333b7eb6e91ae868866cec32d60367",  # smtp.submission.ehlo.auth
    "03e431b9d1dcd96377adf61e22b0228adda36c55ed44e1937984f459a2f7053b",  # smtp.rset.after.auth
    "11b884ece7fd796c5e168916a0ea29b3d9b2799d007d91b9b966eed65557f730",  # smtp.noop.after.auth
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
