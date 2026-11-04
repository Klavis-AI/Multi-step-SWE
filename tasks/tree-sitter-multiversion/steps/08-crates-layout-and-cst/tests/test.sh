#!/usr/bin/env bash
# Verifier frame for the tree-sitter-multiversion task. One copy per step; the
# only per-step differences are the STEP_ID literal below and the tests/
# directory's own checks.json / groups.json / assets.
#
# Everything this script asserts happens at the candidate's documented process
# boundary: it runs the executable the task tells the candidate to produce and
# compares normalised observations against a frozen expectation table. It never
# reads candidate source, never imports candidate code, and never links against
# it, so a candidate is free to implement the milestone in any language.
set -u

STEP_ID="08"

. /tests/helpers.sh

# 1. Identity. Must precede everything: a mis-bound tests/ tree grades the
#    wrong milestone at a score that looks like an ordinary partial result.
require_step "$STEP_ID"

# 2. Lock the reward directory before any candidate code can run.
harden_reward_dir

exec 2>&1
echo "== tree-sitter-multiversion step $STEP_ID verifier =="

# 3. Fixture integrity. The grading corpus is verifier-owned and shipped in the
#    image; if any of it differs from what was frozen, nothing below means what
#    it claims. This also catches a candidate that wrote into /tests.
if ! ( cd /tests && sha256sum --quiet -c assets.sha256 ) ; then
    fail_everything "grading fixture corpus failed its checksum manifest"
fi

# 4. The build contract. TEST_INTERFACE.md requires an executable /app/build.sh
#    that produces an executable /app/bin/tree-sitter. Both halves are the
#    candidate's, so both run unprivileged.
BUILD_TIMEOUT=1800
CHECK_BUDGET=1800

[ -f /app/build.sh ] || fail_everything "/app/build.sh does not exist (see TEST_INTERFACE.md)"

give_app_to_unprivileged
mkdir -p /tmp/ts-home/.tree-sitter /tmp/work
chmod -R 777 /tmp/ts-home /tmp/work

echo "-- building candidate (timeout ${BUILD_TIMEOUT}s)"
build_rc=0
run_unprivileged timeout "$BUILD_TIMEOUT" bash /app/build.sh > /tmp/build.log 2>&1 || build_rc=$?
echo "-- build exit=$build_rc, last 25 lines:"
tail -n 25 /tmp/build.log || true

BIN=/app/bin/tree-sitter
if [ ! -x "$BIN" ]; then
    fail_everything "build.sh exited $build_rc and left no executable at /app/bin/tree-sitter"
fi

# 5. Proof of life, before the battery: an unrunnable binary must abort here
#    rather than produce a page of identical check failures.
if ! run_unprivileged env HOME=/tmp/ts-home timeout 120 "$BIN" --version >/tmp/version.txt 2>&1; then
    echo "-- --version output:"; cat /tmp/version.txt || true
    fail_everything "/app/bin/tree-sitter --version did not exit 0"
fi
echo "-- candidate reports: $(cat /tmp/version.txt | head -1)"

# 6. The battery. The driver runs every check in checks.json against the
#    candidate binary and records a normalised observation per check; it makes
#    no scoring decision.
echo "-- running checks (aggregate budget ${CHECK_BUDGET}s)"
run_unprivileged env HOME=/tmp/ts-home TREE_SITTER_DIR=/tmp/ts-home/.tree-sitter \
    timeout $((CHECK_BUDGET + 120)) \
    python3 /tests/driver.py /tests/checks.json /tests/assets /tmp/work "$BIN" /tmp/results.json \
    || echo "-- driver returned non-zero; partial results are still scored"

if [ ! -s /tmp/results.json ]; then
    fail_everything "the check driver produced no results at all"
fi

# 7. reward = gate x score.
read -r GATE SCORE <<< "$(python3 /tests/score.py /tests/groups.json /tmp/results.json /tmp/details.json)"
python3 - <<'PY'
import json
details = json.load(open("/tmp/details.json"))
print("-- gate: %s" % ("PASS" if details["gate"] == 1 else "FAIL"))
for row in details["gate_checks"]:
    if not row["passed"]:
        print("     gate FAILED: %s  %s" % (row["check"], row["detail"][:120]))
for group in details["scored_groups"]:
    print("-- group %-44s tier=%-12s %d/%d  weight=%.3f  ->%.4f"
          % (group["group"], group["tier"], group["passed"], group["total"],
             group["weight"], group["contribution"]))
    for row in group["checks"]:
        if not row["passed"]:
            print("     failed: %s  %s" % (row["check"], row["detail"][:120]))
if "weight_error" in details:
    print("-- WEIGHT ERROR: %s" % details["weight_error"])
if details.get("budget_exhausted"):
    print("-- NOT RUN (budget exhausted): %s" % ", ".join(details["budget_exhausted"]))
PY

cp /tmp/results.json /logs/verifier/check-results.json 2>/dev/null || true
report_reward "$GATE" "$SCORE" /tmp/details.json
