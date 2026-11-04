#!/usr/bin/env bash
set -u
# Whole-verifier bound (hang-path sweep, 2026-10-13; applied 2026-10-14 with
# the user's approval): end the step with reward 0 before Harbor's timeout_sec
# (2700) would drop it from the mean.
VB_LIMIT=2640
if [ -z "${VB_INNER:-}" ]; then
    mkdir -p /logs/verifier
    VB_LOG=$(mktemp /var/tmp/vb-body.XXXXXX)   # root-only, outside /logs/verifier
    VB_INNER=1 timeout -k 15 "$VB_LIMIT" bash "$0" "$@" < /dev/null \
        > "$VB_LOG" 2>&1
    vb_rc=$?
    if [ "$vb_rc" = 124 ] || [ "$vb_rc" = 137 ]; then
        echo "test.sh: whole-verifier bound ${VB_LIMIT}s reached (rc=$vb_rc)" >> "$VB_LOG"
        printf '{"reward": 0.0}\n' > /logs/verifier/reward.json
    fi
    tail -c 1048576 "$VB_LOG" > /logs/verifier/test-body.log 2>/dev/null
    exit 0
fi
# Harbor starts this script inside /app, the candidate's tree. Python puts
# the working directory on its import path for `python3 -`, so a json.py
# left in /app would run as root below; leave /app and isolate both
# interpreters.
cd /
source /tests/helpers.sh

# ---------------------------------------------------------------------------
# Which rung this script was written for, and which rung's corpus is in the
# image it ended up in.
#
# The nine per-step test.sh files once differed only in one literal, at the
# same size and the same whole-second mtime. Docker's build-context sync
# treats (path, size, mtime) as identity, and GNU tar stores mtime to the
# second, so one step's script could be delivered into a sibling step's
# verifier image -- silently grading a rung against another rung's liveness
# directives, with nothing in the report to show it. Three defences, all here:
#
#   * the value actually used is DERIVED from the corpus that shipped in this
#     image, so a mis-delivered sibling still grades its host step correctly;
#   * the literals below say which rung this file was written for, so a
#     disagreement with the corpus is detected rather than assumed away;
#   * the script logs the rung it ran as, so a recurrence is visible in the
#     verifier log instead of having to be inferred from scores.
#
# The two files also differ in size and their mtimes are minutes apart, so the
# substitution has no key to collide on in the first place.
SCRIPT_STEP="05-go114-116"
SCRIPT_LIVENESS="1.4,1.16"

# The reward file must not be writable by anything the candidate runs.
# Every program the toolchain compiles executes under this verifier as the
# unprivileged case user, and if /logs/verifier is group- or world-writable
# one of them can simply unlink the reward file and write its own -- no
# forgery required, it only has to write last. Whether it is writable
# depends on the umask this script inherited, which is not ours to assume:
# measured at umask 0000, a compiled program replaced the reward and the
# harness reported {"reward":1.0}. So the modes are set explicitly here,
# before any candidate code runs, and stay readable for the harness that
# collects them.
umask 0022
mkdir -p /logs/verifier
chmod 755 /logs/verifier 2>/dev/null || true
# A provisional reward, written before any scoring work starts. If the
# verifier is killed outright -- OOM, wall clock, anything that leaves no
# chance to run report -- Harbor still finds a reward file here and the
# remaining steps of the chain still run. The real score overwrites it.
printf '{"reward":0.000000}\n' > /logs/verifier/reward.json
chmod 644 /logs/verifier/reward.json 2>/dev/null || true

# The liveness directives this step must answer to are the oldest and the
# newest `go` directive its own corpus uses, so they are read off that corpus
# rather than hard-coded per step.
DERIVED=""
NCASES=0
read -r DERIVED NCASES <<<"$(python3 -I - <<'PYEOF' 2>/dev/null
import json, re
cs = json.load(open('/grading/cases.json'))
v = sorted({int(m.group(1)) for m in
            (re.search(r'@go1\.(\d+)$', c['id']) for c in cs) if m})
lv = '' if not v else ('1.%d' % v[0] if len(v) == 1 else '1.%d,1.%d' % (v[0], v[-1]))
print('%s %d' % (lv, len(cs)))
PYEOF
)"

if [ -z "$DERIVED" ]; then
    # Never leave LIVENESS_GO empty: runner.py checks nothing at all for an
    # empty list, so the one all-or-nothing gate in the scoring path would
    # pass vacuously and no report field would say so.
    echo "[test.sh] WARNING: could not read the go directives out of /grading/cases.json;" \
         "falling back to this file's own literal $SCRIPT_LIVENESS" >&2
    DERIVED="$SCRIPT_LIVENESS"
elif [ "$DERIVED" != "$SCRIPT_LIVENESS" ]; then
    echo "[test.sh] WARNING: script/corpus mismatch -- this file was written for step" \
         "$SCRIPT_STEP (liveness $SCRIPT_LIVENESS) but the corpus in this image is a" \
         "different rung's (liveness $DERIVED). Grading against the corpus." >&2
fi
echo "[test.sh] step=$SCRIPT_STEP liveness=$DERIVED cases=$NCASES" >&2

LIVENESS_GO="${LIVENESS_GO:-$DERIVED}" python3 -I /tests/runner.py --cases /grading/cases.json --jobs 7 \
    >/logs/verifier/runner.out 2>/logs/verifier/runner.err

score=0
if [ -f /logs/verifier/score ]; then
    score=$(cat /logs/verifier/score)
fi
tail -c 20000 /logs/verifier/runner.err >&2 || true
report "$score"
# rung 5 of 9: 05-go114-116............
