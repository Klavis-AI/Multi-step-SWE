#!/usr/bin/env bash
set -u
# bash reads a script incrementally *as it executes*, so a candidate that can
# write this file could rewrite the part after the score is decided -- and every
# defence in this file lives in that part.  Wrapping the whole body in one
# function forces bash to parse to the closing brace before it runs anything, so
# the tail that runs is the tail that was there at the start.
#
# The candidate's processes run as `mxbuild` and cannot write /tests at all, so
# this is defence in depth -- `candidate_uid()` is a no-op when the verifier is
# not root (and refuses to run anything when root has no `mxbuild` account), and
# a defence against rewriting must not depend on the one thing that can quietly
# not be in force.
_verify() {
source /tests/helpers.sh

mkdir -p /logs/verifier
# Stated rather than inherited.  The reward file is the only thing Harbor reads
# out of this container, and the one defence that survives the verifier being
# *killed* -- no trap runs on SIGKILL, so whatever is on disk at that instant is
# the score -- is that a candidate's processes could never write it in the first
# place.  They run as `mxbuild`; this says in as many words that the directory
# is root's.  Not 700: the artifact collector has to be able to read it.
chown root:root /logs/verifier && chmod 755 /logs/verifier
cd /tests

# Every transcript this step scores was recorded from the pinned reference
# homeserver at authoring time.  This step starts no federation partner: it
# ships no federation scenario module, and the milestone it measures is entirely
# client-server.  (Steps whose scored set does drive a second live homeserver
# say so in their own test.sh.)
STEP_LABEL=6

# Every constant below is read out of the image this script is running in,
# rather than hard-coded for one step.  Harbor builds each step's verifier with
# that step's `tests/` as the docker build context, and every one of those
# context directories is named `tests`.  Docker's build-context sync treats a
# file as unchanged when (relative path, size, mtime) all match, and these
# per-step scripts are generated from one template -- so siblings can be
# byte-identical in length and written in the same second, and a sibling's copy
# of this file lands in this image instead of its own.  Deriving the step from
# the corpus that is actually present makes such a substitution harmless rather
# than fatal -- the script grades the image it is in, whichever sibling's bytes
# it happens to be made of.
MODULES=$(cd /tests && ls scenarios/step[0-9][0-9]_*.py 2>/dev/null \
    | sed 's|scenarios/|scenarios.|; s|\.py$||' | tr '\n' ' ')
EXPECT_DIR=$(cd /tests && ls -d expect/step[0-9][0-9] 2>/dev/null | head -1)
INDEX=$(cd /tests && ls expect/index-v*.json 2>/dev/null | head -1)
STEP=$(printf '%s' "${EXPECT_DIR#expect/step}" | sed 's/^0*//')

# Steps whose scored set drives a second live homeserver need the pinned
# reference interpreter; step 1's image is the one that ships no synapse venv.
# Ask the image rather than hard-coding which step this is.
PYFLAG=""
if [ -x /opt/synapse/bin/python ]; then
    PYFLAG="--python /opt/synapse/bin/python"
fi

# Which script this is, and which corpus the image handed it, said out loud so
# that a substitution is visible in test-stdout.txt instead of having to be
# reconstructed from a reward of zero.
echo "VERIFIER SCRIPT: authored for step ${STEP_LABEL}; image corpus is step ${STEP:-<none>}"
echo "VERIFIER INPUTS: modules=[${MODULES}] index=[${INDEX:-<none>}] expect=[${EXPECT_DIR:-<none>}]"
if [ -n "$STEP" ] && [ "$STEP" != "$STEP_LABEL" ]; then
    echo "WARNING (INFRA): step ${STEP_LABEL}'s test.sh is running inside step" \
         "${STEP}'s verifier image -- docker's build-context sync delivered a" \
         "sibling copy of this file.  Grading the corpus this image actually" \
         "holds (step ${STEP}), which is the right answer for this image." >&2
fi

# An empty glob is not an empty test set, it is a broken one: say so rather
# than handing argparse nothing and letting the reward fall out as zero.  The
# same is true of a missing expectation index or expectation directory: those
# are this image's own contents, and if they are absent the container was built
# or mounted wrong.  Write the reason into report.json as well as stderr --
# a step that aborts before the runner otherwise leaves no report at all, and
# the failure could not be attributed.
if [ -z "${MODULES// /}" ] || [ -z "$EXPECT_DIR" ] || [ -z "$INDEX" ] || [ -z "$STEP" ]; then
    echo "SCORING FAILED (INFRA): this verifier image is missing its own test" \
         "corpus -- modules=[${MODULES}] index=[${INDEX}] expect=[${EXPECT_DIR}]." \
         "A reward of 0 from here is about the verifier, not the candidate." >&2
    {
        echo "--- ls -la /tests ---";           ls -la /tests           || true
        echo "--- ls -la /tests/scenarios ---"; ls -la /tests/scenarios || true
        echo "--- ls -la /tests/expect ---";    ls -la /tests/expect    || true
    } >&2
    mkdir -p /logs/verifier
    printf '{"reward":0.0,"infra_error":"verifier image has no scenario corpus","step_label":%s,"modules":"%s","index":"%s","expect":"%s"}\n' \
        "$STEP_LABEL" "$MODULES" "$INDEX" "$EXPECT_DIR" \
        > /logs/verifier/report.json
    report 0
fi

# Bounded well under this step's `[steps.verifier] timeout_sec` (3600), so
# that a candidate whose server hangs costs this step its reward rather
# than costing the trial every step after it: a killed verifier writes no
# reward.json at all, and Harbor stops there.
timeout 3000 python3 runner/verify.py \
    --modules $MODULES \
    --step "$STEP" \
    --index "$INDEX" \
    --expect "$EXPECT_DIR" \
    --app /app --work /work --require-shipped DENDRITE \
    --report /logs/verifier/report.json \
    $PYFLAG \
    --workers 6 --port-base 21000 \
    > /logs/verifier/verify.log 2>&1

# No process of the candidate account outlives the scoring.  verify.py reaps
# them itself before it writes the report; this covers the timeout above
# cutting it off before it got there.
timeout 120 python3 -I -c 'import sys; sys.path.insert(0, "/tests/runner")
from mxconf.targets import reap_candidate; reap_candidate()' \
    >> /logs/verifier/verify.log 2>&1 || true

# What the candidate's own code could have reached, said out loud rather than
# assumed: the uid every candidate process runs as, and whatever is left of the
# expectation directory after the verifier read it into memory and deleted it.
echo "VERIFIER IDENTITY: uid=$(id -u) ($(id -un))"
echo "EXPECTATIONS ON DISK: $(ls -la /tests/expect 2>&1 | head -5)"

reward=$(python3 -c "
import json, sys
try:
    print(json.load(open('/logs/verifier/report.json'))['reward'])
except Exception as exc:
    print('SCORING FAILED: no reward in the report (%s). A reward of 0 from '
          'here is about the verifier, not the candidate.' % exc,
          file=sys.stderr)
    print(0.0)
")
tail -n 200 /logs/verifier/verify.log || true
report "$reward"
}

_verify "$@"
# collision-pad: keeps every sibling test.sh a distinct byte length, so docker's
# build-context sync (which calls a file unchanged when relpath+size+mtime match)
# can never hand this step's image a sibling step's copy of this file.-
