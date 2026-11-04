#!/usr/bin/env bash
# Step verifier. Runs in this step's own container on a fresh copy of /app.
# Byte-identical in every step; the step differs only in /tests/STEP,
# /tests/expected/ and /tests/fixtures/.
#
# Never exits non-zero: a verifier crash makes Harbor skip the remaining steps.
set -uo pipefail
source /tests/helpers.sh

# Everything here runs from /, never from the container's working directory,
# which is /app: the candidate's own workspace. `python3 -c` puts the current
# directory first on sys.path, so a json.py left in /app ran as root in place of
# the standard library. Every Python below is also started with -I.
cd / || true

# 0. Whole-run budget. Harbor discards a verifier that outlives this step's
#    timeout_sec (2400) even when reward.json exists, and then aborts every
#    later step, so a candidate that could stall the build or the scorer would
#    turn a step it cannot pass into no result at all. The body therefore runs
#    under a hard limit of RUN_BUDGET seconds, and a body that runs out scores
#    0. RUN_BUDGET + the 20 s kill grace + the sweep in report() stay under
#    timeout_sec - 240 s. Inside it the build gets BUILD_LIMIT seconds, and the
#    scorer's document deadline is what is left, capped at SCORE_DEADLINE
#    (1500): a run whose build fits in BUILD_LIMIT keeps the full 1500 s.
RUN_BUDGET=2120
BUILD_LIMIT=360
if [ "${1:-}" != "--within-budget" ]; then
    harden_reward_dir
    timeout -k 20 "${RUN_BUDGET}" bash /tests/test.sh --within-budget
    rc=$?
    if [ "${rc}" -ne 0 ] || [ ! -s /logs/verifier/reward.json ]; then
        echo "BUDGET: the verifier did not finish inside ${RUN_BUDGET}s (rc=${rc}); scoring 0"
        report 0
    fi
    exit 0
fi

# The step's identity is DERIVED from the name of its own expectation file,
# which is unique per step in both name and content, rather than read out of
# /tests/STEP. /tests/STEP is a tiny file that can share a (size, mtime) with
# another step's copy, and docker's build-context sync treats a file with a
# matching (path, size, mtime) as unchanged, so one step's image can receive
# another step's STEP file. The expectation file is already per-step, and
# grading would fail loudly without it.
STEP=$(basename "$(ls -1 /tests/expected/*.json 2>/dev/null | head -1)" .json)
case "${STEP}" in ''|'*') STEP=$(cat /tests/STEP 2>/dev/null || echo unknown) ;; esac
echo "=== verifier for step ${STEP} ==="
DECLARED=$(cat /tests/STEP 2>/dev/null || echo unknown)
if [ "${DECLARED}" != "${STEP}" ]; then
    echo "NOTE: /tests/STEP says '${DECLARED}' but the expectation file staged here" \
         "is '${STEP}'; grading follows the expectation file."
fi

# 1. The reward file is ours to write, before anything the candidate touched runs.
harden_reward_dir

# 2. Posture. Fails closed: no unprivileged identity means no safe way to run
#    candidate code in this container, so the step scores 0 rather than guessing.
if ! ensure_grader; then gate_fields; echo "POSTURE FAIL: no grader identity"; report 0; fi
gate_fields
if ! candidate_cannot_read_answers; then echo "POSTURE FAIL: sequester not in force"; report 0; fi
if ! verify_tests_manifest; then echo "POSTURE FAIL: grading material altered"; report 0; fi

EXP=$(ls -1 /tests/expected/*.json 2>/dev/null | head -1)
if [ -z "${EXP}" ]; then echo "GATE FAIL: no expectation file staged"; report 0; fi
NDOC=$(python3 -I -c "import json;print(len(json.load(open('${EXP}'))['fixtures']))" 2>/dev/null || echo 0)
NFIX=$(find /tests/fixtures -name '*.html' | wc -l)
echo "expectation file=${EXP} documents=${NDOC} fixtures_on_disk=${NFIX}"
if [ "${NDOC}" -lt 1 ] || [ "${NFIX}" -lt 1 ]; then
    echo "GATE FAIL: empty population (documents=${NDOC} fixtures=${NFIX})"; report 0
fi

# 3. Rebuild the remote end from source, as the unprivileged user, in a copy of
#    the collected workspace. The build is the candidate's own Makefile; we grep
#    the exit status and the artifact, never a compiler diagnostic.
rm -rf /work && mkdir -p /work
cp -a /app/. /work/ 2>/dev/null || true
# The collected /app can carry set-id files (an agent working as root in its
# own container); the candidate later runs as grader without no_new_privs, so
# no set-id file may stay executable for it, in /app or in the copy. chown -R
# below clears the bits in /work; strip them in /app explicitly. find does not
# follow symlinks, and no candidate process exists yet.
find /app /work -xdev -type f -perm /6000 -exec chmod ug-s {} + 2>/dev/null || true
# Grade a build, not an inheritance. `artifacts` collects /app with exclude=[],
# so build/cssdriver and every .o travel from the agent container into this one;
# `make` would then find its targets up to date and score a binary this image
# never compiled. A candidate could ship a working binary of any provenance
# beside sources that do not compile. Drop the inherited outputs so the compile
# always happens here, with this image's toolchain -- and not only build/: every
# compiled artifact, wherever it sits and whatever it is called, so nothing
# prebuilt elsewhere (an object, an archive, a library) can be linked in.
rm -rf /work/build
python3 -I /tests/provenance.py scrub /work
chown -R grader:grader /work
echo "--- build ---"
# provenance.py runs make as grader with outbound network refused, under
# BUILD_LIMIT, and watches what the build executes: rc 0 built, 1 failed or
# timed out, 3 the build ran or mapped code that is neither a system file nor a
# C program built here.
python3 -I /tests/provenance.py build --user grader --timeout "${BUILD_LIMIT}" \
    --log /logs/verifier/build.log -- make -C /work -j7
BUILD_RC=$?
BIN=/work/build/cssdriver
if [ "${BUILD_RC}" -eq 3 ]; then
    echo "PROVENANCE FAIL: the build did not only compile this workspace's C"; report 0
fi
if [ "${BUILD_RC}" -ne 0 ] || [ ! -x "${BIN}" ]; then
    echo "BUILD FAILED rc=${BUILD_RC} binary_present=$([ -x "${BIN}" ] && echo yes || echo no)"
    echo "--- last 40 lines of build log ---"; tail -40 /logs/verifier/build.log
    report 0
fi
echo "build ok: $(ls -l ${BIN} | awk '{print $5}') bytes"

# The deliverable must be the C program the instruction describes: a native
# executable compiled here that links only the C library, with no other
# language's runtime, embedded executable image or existing engine inside it.
if ! python3 -I /tests/provenance.py check --binary "${BIN}" --tree /work; then
    echo "PROVENANCE FAIL: the remote end is not a C program built from this workspace"
    report 0
fi

# 4. Score. The document deadline is what the budget has left, capped at
#    SCORE_DEADLINE; the scorer itself is bounded just beyond it, so a remote end
#    that stalls one request cannot carry the run past RUN_BUDGET.
echo "--- scoring ---"
DEADLINE=${SCORE_DEADLINE:-1500}
LEFT=$(( RUN_BUDGET - SECONDS - 200 ))
if [ "${LEFT}" -lt "${DEADLINE}" ]; then DEADLINE=${LEFT}; fi
if [ "${DEADLINE}" -lt 60 ]; then DEADLINE=60; fi
timeout -k 10 $(( DEADLINE + 150 )) python3 -I /tests/score.py \
    --binary "${BIN}" \
    --expected "${EXP}" \
    --fixtures /tests/fixtures \
    --report /logs/verifier/report.json \
    --run-as grader \
    --deadline "${DEADLINE}" \
    > /logs/verifier/score.out 2>&1
SCORE_RC=$?
cat /logs/verifier/score.out

# 5. Read the number. Check its SHAPE before any clamp, so a non-numeric value
#    cannot arrive as 0 and be mistaken for a scored zero.
RAW=$(awk -F= '/^SCORE=/{v=$2} END{if (v=="") print "MISSING"; else print v}' /logs/verifier/score.out)
case "${RAW}" in
    ''|MISSING) echo "GATE FAIL: scorer produced no SCORE= line (rc=${SCORE_RC})"; report 0 ;;
    *[!0-9.]*|*.*.*) echo "GATE FAIL: SCORE= is not a plain number: '${RAW}'"; report 0 ;;
esac
echo "scorer rc=${SCORE_RC} score=${RAW}"

# The grading material must still be the material we started with. A candidate
# that rewrote a test between the two checks does not get to keep its score.
if ! verify_tests_manifest; then echo "POSTURE FAIL: /tests altered during scoring"; report 0; fi
report "${RAW}"
