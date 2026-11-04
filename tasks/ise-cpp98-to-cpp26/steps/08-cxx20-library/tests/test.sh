#!/usr/bin/env bash
# Harbor verifier entrypoint for one step. Written by
# authoring/render_package.py into every step's tests/; edit
# authoring/verifier/test.sh.
#
# Build the candidate tree, then score it. Both halves are allowed to fail: a
# tree that does not compile still gets its gates run and still produces a
# reward.json, because "the verifier crashed" and "the candidate scored zero"
# must not look the same to a later reader.
set -u

# Whole-verifier bound (hang-path sweep, 2026-10-13): end the step with reward 0
# before Harbor's verifier timeout_sec (3600 s) would drop it from the mean and
# abort the later steps. It sits above this step's own per-phase deadline
# (HARBOR_TIMEOUT - RESERVE), so it fires only if a phase bound is itself blocked
# -- an unbounded root read a reap missed. The body's output goes to a file, so
# an orphan holding Harbor's stream cannot keep it waiting.
VB_LIMIT=3540
if [ -z "${VB_INNER:-}" ]; then
    mkdir -p /logs/verifier
    VB_INNER=1 timeout -k 15 "$VB_LIMIT" bash "$0" "$@" < /dev/null \
        > /logs/verifier/test-body.log 2>&1
    vb_rc=$?
    cat /logs/verifier/test-body.log 2>/dev/null || true
    if [ "$vb_rc" = 124 ] || [ "$vb_rc" = 137 ]; then
        echo "test.sh: whole-verifier bound ${VB_LIMIT}s reached (rc=$vb_rc)" \
            >> /logs/verifier/test-body.log
        chown root:root /logs/verifier 2>/dev/null || true
        chmod 755 /logs/verifier 2>/dev/null || true
        printf '{"reward": 0.0}\n' > /logs/verifier/reward.json
        chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    fi
    exit 0
fi

. /tests/helpers.sh

# Out of /app before anything runs. Harbor starts this script with the
# verifier's workdir, /app, as its working directory, and /app is the
# candidate's tree: a `python3 -` started from there puts that directory first
# on its import path, so a json.py beside the sources would be what root
# imports (reward-hacking audit 2026-10-12, theme T1). Every path below is
# absolute.
cd /

# Order matters. harden_reward_dir runs before anything the candidate wrote,
# require_app before we conclude anything from an empty tree, and
# give_app_to_unprivileged before the first unprivileged command.
harden_reward_dir
lock_expectations
require_app
give_app_to_unprivileged

echo "=== 08-cxx20-library verifier, $(date -u +%FT%TZ)"

# Every file this verifier writes carries its OWN step number. That is not
# cosmetic: the local docker environment does not reliably build one verifier
# image per step, and a container running an EARLIER step's test.sh scores the
# earlier step's requirements and reports a confident 1.0. The reference
# implements every milestone, so nothing else about that run looks wrong --
# full coverage, n_trials 1, every step on target. The one thing that gives it
# away is a verifier directory whose artifacts are all named for some other
# step, so the names are the evidence and must never be step-agnostic.
BUILD_LOG=/logs/verifier/step08-build.log
: > "$BUILD_LOG"
# The build directory build_candidate.sh defaults to. Named here as well
# because root has to stamp the graph in it; keep the two in step.
BUILD_DIR=/app/build

# One phase of the build.
#
# run_unprivileged OUTSIDE timeout, not inside it. It is a shell function, and
# `timeout` is a binary that execs its argument: with the two the other way
# round the whole build is replaced by `timeout: failed to run command
# 'run_unprivileged': No such file or directory`, a 77-byte build log, a
# build_rc of 127, and a score that still has every text gate in it -- 0.54 on
# a tree that deserves 1.0, with nothing in the reward that says the verifier
# never compiled anything. Measured, on the reference solution, before the
# order was swapped. timeout runs as nobody and kills a child of the same
# user, so dropping privilege first costs nothing.
#
# The log is written through a pipe rather than by redirecting the build's
# output into it. `> "$BUILD_LOG"` is root's open file, but the descriptor is
# inherited by every process the build starts, and a custom command that
# ftruncates fd 1 leaves a verifier log saying whatever the build wanted a
# later reader to see. The write end of a pipe cannot be truncated or seeked,
# and the only process holding the file itself is root's own `cat`. That cat
# is bounded too: a detached writer keeps the pipe open after the build has
# been killed, and the reap that would deal with it comes afterwards.
#
# Nothing the build detached may outlive it and race the scorer, so each phase
# is reaped before anything else runs; see reap_unprivileged in helpers.sh for
# what such a process would still be able to write.
#
# The trace sink goes down fd 3, and only on the configure. Root opens it
# here, so the descriptor the configure inherits is one it could not have
# opened for itself -- the sink's directory is root's and mode 700 -- and
# `--trace-redirect=/dev/fd/3` is then a name that means nothing to any other
# process. See open_trace_sink in helpers.sh and traceguard.c. The redirect is
# on the build command inside the pipeline rather than on the whole function,
# so the `cat` holding the log does not inherit it either.
#
# The descriptor is also the whole of what build_candidate.sh is told: fd 3 is
# this file on the configure and /dev/null on every other phase, and nothing
# names either one in the environment the candidate's code runs in.
build_phase() {
    local phase=$1 budget=$2 rc sink=/dev/null
    if [ "$phase" = configure ] && [ -f "${ISEFIX_TRACE_FILE:-/nonexistent}" ]
    then
        sink=$ISEFIX_TRACE_FILE
    fi
    run_unprivileged timeout --signal=KILL "$budget" \
        bash /tests/build_candidate.sh "$phase" 3>>"$sink" 2>&1 \
        | timeout --signal=KILL "$((budget + 60))" cat >> "$BUILD_LOG"
    rc=${PIPESTATUS[0]}
    reap_unprivileged
    return "$rc"
}

# Three phases with root between each pair, because none of what root takes
# here is evidence unless it is taken where the tree being scored cannot reach
# it -- and each piece has to be taken at the one moment when nothing of the
# candidate's is running. After the sweep, the tree is digested; before the
# configure, the trace sink is opened; between the configure and the build,
# the graph is stamped. See snapshot_tree, open_trace_sink and
# stamp_build_graph in helpers.sh.
#
# Phase budgets, in seconds. Each is a deadline for a hang, not an estimate:
# every phase has been measured at a small fraction of its own budget.
#
# What matters is which deadline fires first. Harbor gives the whole verifier
# one, and if that is the one that expires the container is destroyed with no
# reward.json in it: the step disappears from the mean (trap 3) and nothing
# anywhere records which phase hung. An inner budget expiring instead kills
# one phase, lets the rest run, and produces a real score next to a log that
# says what died.
#
# So the inner deadlines are made to come first by construction rather than by
# arithmetic: each phase is held to the smaller of its own budget and the time
# actually left before Harbor's, and grade.py is guaranteed a floor. Every
# phase keeps a budget generous enough for a slow candidate, no step has to
# trade one phase's headroom against another's, and the ordering does not have
# to be re-derived each time a step adds a phase.
#
# A step whose scoring genuinely costs more says so in its own step.json --
# `"budget": {"grade": 1800}` -- rather than raising the ceiling for the ten
# steps that do not need it. check_budgets.py checks the declarations against
# task.toml.
eval "$(python3 -I -B - <<'BUDGETS'
import json
DEFAULT = {"clean": 120, "sanitize": 900, "configure": 300,
           "compile": 1500, "grade": 900}
cfg = json.load(open("/tests/step.json"))
asked = cfg.get("budget") or {}
unknown = sorted(set(asked) - set(DEFAULT))
assert not unknown, "step.json asks for unknown phase budget(s): %s" % unknown
# Names from DEFAULT and values through int(): step.json is verifier-owned and
# root-only, but a shell `eval` of generated text is not the place to take
# that on trust.
for name in sorted(DEFAULT):
    print("BUDGET_%s=%d" % (name.upper(), int(asked.get(name, DEFAULT[name]))))
print("WANT_SANITIZE=%d" % (1 if cfg.get("sanitize") else 0))
# Harbor's own deadline for this step. Declared here so the verifier can see
# it; check_budgets.py asserts it is the number task.toml actually sets, so
# the two cannot drift apart unnoticed.
print("HARBOR_TIMEOUT=%d" % int(cfg.get("harbor_timeout", 3600)))
BUDGETS
)"
: "${BUDGET_CLEAN:?budget block produced nothing}"

# Held back from the phases: the work root does between them -- the tree
# digest, the graph stamp, the trace sink -- plus writing reward.json and
# Harbor's own container teardown. Measured at about 20 s in total; this is
# nine times that.
RESERVE=180
# A phase is never given less than this, even when the reserve is already
# gone. A grade that is killed on the spot reports nothing; one with half a
# minute reports that it ran out.
FLOOR=30
STARTED=$SECONDS

# The smaller of a phase's own budget and the time left before Harbor's.
budget_for() {
    local want=$1 left
    left=$(( HARBOR_TIMEOUT - RESERVE - (SECONDS - STARTED) ))
    [ "$left" -lt "$FLOOR" ] && left=$FLOOR
    if [ "$want" -lt "$left" ]; then echo "$want"; else echo "$left"; fi
}

echo "=== budgets clean=$BUDGET_CLEAN sanitize=$BUDGET_SANITIZE($WANT_SANITIZE) configure=$BUDGET_CONFIGURE compile=$BUDGET_COMPILE grade=$BUDGET_GRADE harbor=$HARBOR_TIMEOUT"

# The clean phase's exit status is not the build's: it deletes stale output
# and reports the toolchain, and a tree with nothing to delete and a tree
# whose sweep hit an unreadable directory are both trees the configure should
# still be asked about. It is logged and passed over.
build_phase clean "$(budget_for "$BUDGET_CLEAN")"
echo "clean rc=$?"
snapshot_tree /app /logs/verifier/step08-snapshot.log

# The instrumented second build, for the steps whose gates run the programs
# under the compiler's own allocation checking. Asked for by step.json rather
# than by the step number, so a step that does not need it does not pay four
# minutes for it.
#
# After the snapshot, not before. This phase runs the project's own CMake, and
# run_gates decides once -- at entry, before the first gate -- whether the
# tree still matches what root digested. A configure that ran before the
# digest would be candidate code writing the tree in the one window that
# comparison cannot see.
#
# Before the trace sink and the graded configure, and its failure is not the
# build's: what this produces is read by one gate, which reports the build log
# when there is nothing there, and a tree that cannot be instrumented must
# still be scored on everything else.
if [ "$WANT_SANITIZE" = 1 ]; then
    build_phase sanitize "$(budget_for "$BUDGET_SANITIZE")"
    echo "sanitize rc=$?"
fi

open_trace_sink /logs/verifier/step08-trace.log

# The stamp runs even when the configure failed and there is nothing to stamp:
# what it exports is how grade.py knows a stamp was meant to exist, and a
# verifier that quietly fell back to deriving the database from the finished
# build would be the hole this closes, reachable by any configure that
# arranges to fail after doing its own work.
build_phase configure "$(budget_for "$BUDGET_CONFIGURE")"
BUILD_RC=$?
stamp_build_graph "$BUILD_DIR" /logs/verifier/step08-graph.log
if [ "$BUILD_RC" -eq 0 ]; then
    build_phase compile "$(budget_for "$BUDGET_COMPILE")"
    BUILD_RC=$?
fi
echo "build rc=$BUILD_RC ($(wc -c < "$BUILD_LOG") bytes of log)"
# Nothing the candidate runs from here on writes the tree; see seal_app.
seal_app
if [ "$BUILD_RC" -ne 0 ]; then
    tail -40 "$BUILD_LOG"
fi

# build_candidate.sh exits 10 (configure failed) or 11 (compile failed), and
# timeout exits 137 when it kills. Anything else in the reserved band is the
# harness failing to start the build rather than the build failing, and the
# two must not arrive at the reader as the same number.
case "$BUILD_RC" in
    125 | 126 | 127)
        {
            printf 'VERIFIER-PROBLEM: the build was never started (rc=%s).\n' \
                "$BUILD_RC"
            cat "$BUILD_LOG"
        } > /logs/verifier/step08-build-not-started.log
        ;;
esac

# The score. grade.py runs as root because it owns /logs/verifier; everything
# it starts on the candidate's behalf carries the unprivileged prefix.
GRADE_LOG=/logs/verifier/step08-stderr.log
SCORE=$(PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX=/tmp/pycache \
        ISEFIX_LAUNCH_PREFIX="$UNPRIVILEGED_PREFIX" \
        timeout --signal=KILL "$(budget_for "$BUDGET_GRADE")" \
        python3 -s -E -B /tests/grade.py --build-rc "$BUILD_RC" 2> "$GRADE_LOG")
GRADE_RC=$?
cat "$GRADE_LOG"

if [ "$GRADE_RC" -ne 0 ] || [ -z "$SCORE" ]; then
    # Scoring itself broke. Say so in a file, and score 0 -- but the file is
    # the point: a 0 with grade-crashed.log beside it is a verifier bug, and a
    # 0 without one is a candidate that earned it.
    {
        printf 'VERIFIER-PROBLEM: grade.py exited %s with score %s\n' \
            "$GRADE_RC" "${SCORE:-<empty>}"
        tail -60 "$GRADE_LOG"
    } > /logs/verifier/step08-grade-crashed.log
    report 0
fi

echo "=== reward $SCORE"
report "$SCORE"

# build-context guard (2026-10-05): every step's verifier build context is a
# directory named tests, and BuildKit reuses its cached copy of a context
# file whose relpath, size, mode and mtime all match, so a file that differs
# between steps only in its step id can reach an image as another step's
# copy. The pad below keeps this file's size unique per step (sizes survive
# git and tar; mtimes do not). It is a comment and changes no behaviour; keep
# a per-step pad like it if this file is edited or regenerated.
# step 08 pad: ================================================================================
