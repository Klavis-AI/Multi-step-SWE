#!/usr/bin/env bash
# Step 4's verifier.
#
#   reward = A * B
#     A  behaviour equivalence  0.85 * recorded corpus + 0.15 * inherited suite
#     B  migrated-ness gates    mean(new) * all(carried), zero if the tree
#                               does not import at all
#
# The families multiply because the tree the step is handed passes A completely
# and fails B completely by construction, and that tree must
# score 0. See skutil_gates/grade.py for the argument in full.
#
# The expectation is still step 1's recording -- the same 1236 values, recorded
# once, never re-recorded -- but the step-4 stack (pandas 2.2.3, pyarrow
# 17.0.0, scikit-learn 1.6.1, scipy 1.13.1 and NumPy 1.26.4) genuinely changed 160 of them relative
# to what step 1 produced, and a verifier that demanded the old answers would be
# asking for a migration that pins the old libraries back. Those 160 are
# declared one at a time in expected/flips-step04.json, each with the library
# change that caused it and a citation, and `skutil_probe.run compare --flips`
# applies them to the expectation before the comparison. Nothing else about the
# corpus moves, and every case not named in that file must still produce exactly
# what step 1 recorded.
#
# The two populations are scored apart, not pooled. The corpus fraction is the
# share of the 1076 unchanged cases that still match, times the share of the
# 160 flips that landed at their new answer -- must-stay x must-flip, the same
# product shape B uses. Pooling them would dilute a reverted flip to one row
# in 1236; scoring the flips as their own population makes reverting one cost
# against 160. grade.py is told
# how many flips to expect (--min-flips, from manifest.json), so a submission
# whose import stops the flips file from loading scores its corpus 0 rather
# than being handed an all-must-stay corpus it would pass.
#
# Nothing here decides a score. Each stage writes a JSON file and
# `skutil_gates.grade` combines them, so any number in the log can be
# recomputed from the saved bundle without re-running the container.
#
# The file is organised around one distinction: a stage either runs the
# candidate's code or it does not.
#
#   observing stages  import or execute skutil. They run through run_stage --
#                     uid 65534, with a 15-minute budget each so that a hang
#                     costs one family instead of the whole step -- they write
#                     into $WORK, and they never see an expectation:
#                     /tests/expected and /tests/manifest.json are root-only
#                     (see the Dockerfile).
#
#   scoring stages    are pure arithmetic over JSON. Nothing they import
#                     touches skutil, so they run as root against the
#                     expectations, reading each observing stage's output out
#                     of $OUT -- a root-owned copy taken by collect() the
#                     moment that stage exits, so a process the candidate left
#                     running cannot edit its own answer sheet afterwards.
#
# Three further orderings are load-bearing:
#
#  1. The package is built and installed from a COPY of /app, never from /app.
#     `pip` leaves `build/lib.*/`, a complete second copy of every module, in
#     the directory it builds in -- and the gate scanners exclude nothing, so
#     a tree that was built in place reports every finding twice.
#  2. Every stage runs with the working directory in scratch, never /app. From
#     /app, `import skutil` finds the source directory instead of the installed
#     package, and the probes would measure a tree that was never built.
#  3. `harden_reward_dir` runs first, and nothing the candidate built is ever
#     installed into, or run by, the scorer's own interpreter: the wheel goes
#     into $CAND, a virtual environment uid 65534 creates and fills, and only
#     the processes that run candidate code start its interpreter, $CPY (see
#     helpers.sh:install_candidate_wheel). The build that runs setup.py is
#     unprivileged too.
set -u

# Whole-verifier bound: end the step with
# reward 0 before Harbor's timeout_sec would drop it from the mean.
# timeout_sec is 1800 at every step; the body's own DEADLINE (below) ends the
# candidate stages at 1560 s, so an honest run finishes well inside this.
VB_LIMIT=1740
if [ -z "${VB_INNER:-}" ]; then
    mkdir -p /logs/verifier
    VB_LOG=$(mktemp /var/tmp/vb-body.XXXXXX)   # root-only, outside /logs/verifier
    VB_INNER=1 timeout -k 15 "$VB_LIMIT" bash "$0" "$@" < /dev/null \
        > "$VB_LOG" 2>&1
    vb_rc=$?
    if [ "$vb_rc" = 124 ] || [ "$vb_rc" = 137 ]; then
        echo "test.sh: whole-verifier bound ${VB_LIMIT}s reached (rc=$vb_rc)" >> "$VB_LOG"
        printf '{"reward": 0.0}\n' > /logs/verifier/reward.json
        # as report() does, so a non-root collector can read it
        chmod 755 /logs/verifier 2>/dev/null || true
        chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    fi
    tail -c 1048576 "$VB_LOG" > /logs/verifier/test-body.log 2>/dev/null
    chmod 644 /logs/verifier/test-body.log 2>/dev/null || true
    exit 0
fi

. /tests/helpers.sh

# Harbor starts this script with cwd /app, the candidate's tree. A root
# `python -c`/`python -m` started from there has /app on sys.path ahead of the
# standard library, so a planted /app/json.py would run as root. Every root
# interpreter below starts from / (or uses -I where it runs from $WORK).
cd / || report 0

harden_reward_dir

# The whole candidate phase is held under the step's [steps.verifier]
# timeout_sec. Harbor discards a verifier that overruns -- even one that wrote
# reward.json -- and drops the step from the mean while aborting every step
# after it, so the per-stage caps must not be allowed to *sum* past the step's
# budget. DEADLINE is an epoch second comfortably under timeout_sec (1800 here,
# 240 kept back for the image build and the /app round trip Harbor counts in
# the same window); _cap holds every candidate stage, and every scoring stage,
# to what is left of it. See helpers.sh:_cap.
DEADLINE=$(( $(date +%s) + 1560 ))
export DEADLINE
stamp_step 04 "$(python -c \
    'import json; print(json.load(open("/tests/manifest.json"))["step"])' \
    2>/dev/null)"
make_scratch
export_frozen_env

note "step 4 verifier: behaviour corpus + inherited suite + migrated-ness gates"

# An /app the artifact never reached. Such a tree fails every gate for
# reasons that have nothing to do with the submission, and a bare 0 in
# reward.json looks exactly like an honest failure. Say so in the log instead.
py_files=$(find /app -name '*.py' -type f 2>/dev/null | wc -l)
note "/app holds ${py_files} python files (the pristine tree has 58)"
if [ "${py_files}" -lt 30 ]; then
    note "FATAL: /app is not a skutil tree -- artifact missing or emptied"
    find /app -maxdepth 2 2>/dev/null | head -40 | while read -r p; do
        note "  /app entry: ${p}"
    done
    report 0
fi

# The tree is graded exactly as it was handed over, minus whatever the agent's
# own builds left lying around. See clean_build_products() for why each of those
# would otherwise move a count.
clean_build_products /app
chmod -R a+rX /app 2>/dev/null || true

# Build from the copy; /app stays pristine for the scanners. The build's log
# lives in the root-owned $OUT, not in $WORK, which uid 65534 owns: a FIFO
# left at that name would block root's tail and append below.
cp -a /app "$WORK/src"
chown -R "$SCORE_UID:$SCORE_GID" "$WORK" 2>/dev/null || true

note "building the candidate tree from ${WORK}/src"
if run_stage 900 python -m pip wheel --no-build-isolation --no-deps \
        --wheel-dir "$WORK/wheel" "$WORK/src" \
        > "$OUT/build.log" 2>&1; then
    note "wheel built"
else
    note "WARNING: the candidate tree did not build"
    tail -n 30 "$OUT/build.log" | while read -r line; do
        note "  [build] ${line}"
    done
fi

whl=$(ls -1 "$WORK"/wheel/*.whl 2>/dev/null | head -1 || true)
if [ -n "${whl:-}" ]; then
    # Unprivileged, into the candidate's own environment ($CAND), never into
    # the interpreter the root scoring stages run: whatever the wheel's RECORD
    # lists lands where only candidate processes look.
    install_candidate_wheel "$whl" >> "$OUT/build.log" 2>&1 \
        || note "WARNING: installing ${whl} failed"
else
    note "WARNING: no wheel was produced; the runtime families will score 0"
fi

# Where does `import skutil` resolve now that the wheel is installed? A
# diagnostic -- but importing the candidate package runs its
# skutil/__init__.py, and run as root that is candidate code executing as
# root: it can overwrite this harness, grade.py included, and no launcher
# defends a file that has already been physically rewritten. So it drops to uid 65534 through run_stage, exactly like every other
# stage that touches candidate code, and it runs from $OUT -- root-owned and
# empty at this point -- rather than $WORK, so neither a $WORK/skutil.py the
# build may have dropped nor anything under it can shadow the installed
# package on sys.path[0]. uid 65534 imports the same installed skutil in the
# observing stages, so the path this prints is the one those stages see.
installed=$(cd "$OUT" && run_stage 120 "$CPY" -c \
    'import os, skutil; print(os.path.dirname(os.path.abspath(skutil.__file__)))' \
    2>/dev/null || true)
note "importable package: ${installed:-<none>}"

# From here on the working directory is the unprivileged scratch (ordering note
# 2 above).
#
#  4. Every harness stage below runs `python /tests/harness/_run.py <module>`,
#     never `python -m <module>`. The cwd here ($WORK) is owned by the uid that
#     runs candidate code, and `python -m` would put that cwd on sys.path ahead
#     of PYTHONPATH -- as sys.path[0]='' -- so a skutil_gates/ or skutil_probe/
#     (or a runpy.py) a candidate left in $WORK would be imported in preference
#     to /tests/harness, including by the root scoring stages whose stdout is
#     the reward. _run.py is a script, so sys.path[0] is /tests/harness and the
#     cwd is never consulted. See harness/_run.py.
cd "$WORK" || report 0

# ---------------------------------------------------------------- family A.1
# The recorded corpus: 1236 values this tree produced before anybody touched it,
# read through this step's 160 declared flips.
note "replaying the behaviour corpus"
run_stage 900 "$CPY" /tests/harness/_run.py skutil_probe.run record --out "$WORK/actual.json" \
    || note "WARNING: the behaviour recording did not complete"
collect "$WORK/actual.json" "$OUT/actual.json" \
    || note "WARNING: no behaviour recording to collect"

scoring_stage 300 python /tests/harness/_run.py skutil_probe.run compare \
    --expected /tests/expected/baseline-recording.json \
    --flips /tests/expected/flips-step04.json \
    --actual "$OUT/actual.json" \
    --out "$OUT/behavior.json" \
    || note "WARNING: the behaviour comparison did not complete"

# ---------------------------------------------------------------- family A.2
# The repository's own tests. Scored as an outcome table rather than as a
# pristine overlay -- see skutil_gates/suite.py for why this migration cannot
# use the latter.
note "running the inherited test suite"
run_stage 900 "$CPY" /tests/harness/_run.py skutil_gates.suite capture \
    --out "$WORK/suite-observed.json" \
    || note "WARNING: the inherited suite did not complete"
collect "$WORK/suite-observed.json" "$OUT/suite-observed.json" \
    || note "WARNING: no suite capture to collect"

scoring_stage 300 python /tests/harness/_run.py skutil_gates.suite compare \
    --expected /tests/expected/inherited-suite.json \
    --observed "$OUT/suite-observed.json" \
    --out "$OUT/suite.json" \
    || note "WARNING: the inherited suite scoring did not complete"

# ------------------------------------------------------------------ family B
# `--no-installed-root`: the installed copy was built from /app by this script,
# so scanning both would count every construct twice.
note "observing the migrated-ness gates"
#
# Two stages, so the static scans are taken in a process tree no candidate code
# runs in (`--no-repro`, the scorer's own interpreter), and the runtime probes,
# whose children start the candidate's interpreter $CPY, run on their own. Each
# writes its bundle through fd 3, which root opened on a root-owned file in
# $OUT: the stage writes through the descriptor it was handed, nothing it
# spawns inherits it, and no process of uid 65534 can open that file for
# writing, so nothing the candidate leaves running can edit a bundle before
# the stage is reaped.
run_stage 900 python /tests/harness/_run.py skutil_gates.run observe \
    --tree /app --step 4 --no-installed-root --no-repro \
    --recording "$OUT/actual.json" \
    --out fd:3 3>"$OUT/obs.json" \
    || note "WARNING: the gate observation did not complete"
run_stage 900 python /tests/harness/_run.py skutil_gates.run repro \
    --tree /app --python "$CPY" \
    --out fd:3 3>"$OUT/repro.json" \
    || note "WARNING: the runtime probes did not complete"

scoring_stage 300 python /tests/harness/_run.py skutil_gates.run score \
    --obs "$OUT/obs.json" --manifest /tests/manifest.json \
    --repro "$OUT/repro.json" \
    --out "$OUT/gates.json" \
    || note "WARNING: the gate scoring did not complete"

# -------------------------------------------------------------------- reward
# The corpus is scored as must-stay x must-flip, so the grader is told how
# many flips this step declared. A submission whose `import skutil` stopped
# the flips file from loading would otherwise hand the grader an all-must-stay
# corpus and be scored on it; --min-flips turns that into a corpus of 0.
# manifest.json is the single source of truth for the count -- selfcheck.py
# ties it to the flips file, and the compare stage above applied exactly
# these flips into behavior.json.
min_flips=$(python -I -c \
    'import json; print(int(json.load(open("/tests/manifest.json")).get("flips_expected", 0)))' \
    2>/dev/null || echo 0)
scoring_stage 300 python /tests/harness/_run.py skutil_gates.grade \
    --behavior "$OUT/behavior.json" \
    --suite "$OUT/suite.json" \
    --gates "$OUT/gates.json" \
    --min-flips "$min_flips" \
    --out "$OUT/report.json" > "$OUT/grade.txt" 2>&1 \
    || note "WARNING: the grader did not complete"
while read -r line; do note "${line}"; done < "$OUT/grade.txt"

# The reward is the number grade.py computed and wrote, as root, into
# $OUT/report.json before it printed anything; it is never parsed out of
# grade.txt. The printed lines quote text the candidate's stages chose (a
# suite error, a casebook name), so a forged "REWARD" line among them, followed
# by anything that stopped grade.py before its own last line, would otherwise be
# read as the score. A report that is missing, unreadable, or whose
# reward is not a finite number in [0, 1] scores 0. -I: the cwd here is $WORK,
# which uid 65534 owns.
reward=$(python -I -c '
import json, math, sys
try:
    with open(sys.argv[1]) as fh:
        value = json.load(fh)["reward"]
    ok = (isinstance(value, (int, float)) and not isinstance(value, bool)
          and math.isfinite(value) and 0.0 <= value <= 1.0)
except Exception:
    ok = False
print("%.6f" % value if ok else "0")
' "$OUT/report.json" 2>/dev/null || echo 0)
note "reward ${reward}"

# Kept next to reward.json so a score that looks wrong can be read back without
# re-running the container. Step-stamped for the same reason as the log above.
# Copied only while it is small: it quotes text the candidate's stages chose
# (a suite error can be hundreds of megabytes), and everything under
# /logs/verifier is downloaded inside the step's timeout.
if [ "$(stat -c %s "$OUT/report.json" 2>/dev/null || echo 0)" -le 1048576 ]; then
    cp "$OUT/report.json" /logs/verifier/step04-report.json 2>/dev/null || true
else
    note "report.json is over 1 MiB; not copied next to reward.json"
fi
chmod 644 /logs/verifier/step04-report.json 2>/dev/null || true

report "${reward}"
