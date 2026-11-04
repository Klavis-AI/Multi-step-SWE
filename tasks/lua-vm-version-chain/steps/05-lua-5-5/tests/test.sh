#!/usr/bin/env bash
# Grades one milestone: the workspace must build with `make`, the result must be
# a Java implementation, and `./lua` must behave like the reference interpreter
# on the hidden conformance corpus.
#
# Everything the candidate wrote runs unprivileged -- the build as `builder`,
# every graded program as `candidate` -- because both are candidate-supplied
# code, /tests holds the answers and /logs holds the reward.
set -u
source /tests/helpers.sh

# The entire body is one function, invoked only after bash has parsed it to the
# end. bash reads a script incrementally as it runs, so an entrypoint whose tail
# is still unread could be rewritten by candidate code to change what executes
# after the score is decided; parsing the whole body up front closes that. (In
# this package the candidate is unprivileged and /tests is root-only, so it
# cannot write this file at all -- this is defence in depth.)
_verify() {
LOG=/logs/verifier
mkdir -p "$LOG"
# Root-owned so a graded process (an unprivileged uid) can neither overwrite the
# reward file nor create anything beside it -- write needs the dir's write bit,
# which only root has. The directories stay world-traversable (755) so the host
# can read the reward it collects; secrecy lives in /tests, not here.
chown -R root:root /logs 2>/dev/null || true
chmod 755 /logs "$LOG" 2>/dev/null || true
# A provisional result, so that a verifier the kernel kills still leaves a
# reward file: without one Harbor treats the step as failed and skips the rest
# of the chain, which would lose every later step's result as well. It also
# replaces any file that is already there rather than trusting it.
printf '{"reward":0.000000}\n' > "$LOG/reward.json"
# The grader owns its own deadline: it stops starting programs with enough time
# left to write a real reward file, rather than being killed with none.
T0=$(date +%s)
BUDGET=4800

BUILD_USER=builder
RUN_USER=candidate

# The whole grading model rests on starting as root with the two unprivileged
# users present: a silently re-provisioned container that lacks either would
# turn every privilege drop into a no-op and hand a do-nothing candidate a
# perfect score. Assert the preconditions and fail closed -- a false 0 here is
# correct; a false 1 is the failure being prevented.
if [ "$(id -u)" != 0 ]; then
    echo "verifier is not running as root (uid $(id -u)); cannot enforce the boundary" >&2
    report 0
fi
for u in "$BUILD_USER" "$RUN_USER"; do
    if ! id "$u" >/dev/null 2>&1; then
        echo "unprivileged user '$u' is missing; the privilege drop would be a no-op" >&2
        report 0
    fi
done
# And prove the drop actually lands: a program run as $RUN_USER must NOT be able
# to write the reward directory. If it can, the boundary is not there.
if setpriv --reuid="$RUN_USER" --regid="$RUN_USER" --clear-groups -- \
        /bin/sh -c 'echo x > /logs/verifier/.probe' 2>/dev/null; then
    rm -f /logs/verifier/.probe
    echo "an unprivileged process could write /logs/verifier; boundary absent" >&2
    report 0
fi

# as_user <user> <shell command>: run candidate-supplied code with no
# privileges and none of this shell's environment beyond PATH and HOME.
# Each run is capped at 900 s AND at what is left of BUDGET, so a build
# started late -- the compatibility-matrix builds run after a corpus that may
# already have used the budget up -- cannot carry the verifier past its own
# timeout. A build cut short this way fails, and its programs score as never
# run.
as_user() {
    local u="$1"; shift
    local cap=$(( T0 + BUDGET - $(date +%s) ))
    [ "$cap" -gt 900 ] && cap=900
    [ "$cap" -lt 1 ] && cap=1
    timeout --foreground -k 5 "$cap" \
        setpriv --reuid="$u" --regid="$u" --clear-groups --inh-caps=-all -- \
        env "HOME=/home/$u" TMPDIR=/tmp "PATH=$PATH" /bin/sh -c "$*"
}

cd /app || report 0

# The corpus ships as one archive: a few hundred small files would otherwise be
# a few hundred entries in the verifier image's build context. It is unpacked
# before anything of the candidate's runs, so that the fingerprint below covers
# the tree exactly as the scoring will read it.
if [ ! -d /tests/corpus ]; then
    mkdir -p /tests/corpus
    if ! tar xzf /tests/corpus.tgz -C /tests/corpus; then
        echo "could not unpack the conformance corpus" >&2
        report 0
    fi
fi
if [ ! -f /tests/corpus/expected.json ]; then
    echo "conformance corpus is missing expected.json" >&2
    report 0
fi
chmod -R go-rwx /tests

# What is scored against must be the same bytes at the end as at the start.
# Reading the expectations would earn a good score; rewriting them would earn
# any score at all, so the commitment is checked rather than assumed.
FP_BEFORE=$(python3 -I /tests/sweep.py fingerprint /tests)
echo "tests fingerprint: $FP_BEFORE" > "$LOG/integrity.txt"

echo "=== language purity (pre-build) ===" | tee "$LOG/purity.txt"
if ! python3 -I /tests/purity.py pre /app >>"$LOG/purity.txt" 2>&1; then
    cat "$LOG/purity.txt"
    report 0
fi

# The workspace becomes the build user's for the duration of the build: a tree
# the build has to write in cannot be protected by mode bits alone.
chown -R "$BUILD_USER:$BUILD_USER" /app

# The graded copy has lost every .class file (purity pre above) and ./lua,
# build/ and gen/ (task.toml excludes), but it keeps any other build-state
# file -- a `classes/.stamp`, say -- so a plain `make` can decide it is up to
# date and compile nothing. Start from the candidate's own `make clean`, the
# same clean-then-build the pass-through and matrix builds below already
# require. Its status is not judged here (a `rm -r build` for a directory the
# copy does not have must not zero a working tree); the `make` that follows is.
echo "=== make clean; make ===" > "$LOG/build.log"
if ! as_user "$BUILD_USER" 'cd /app && { make clean || true; } && exec make' >>"$LOG/build.log" 2>&1; then
    tail -40 "$LOG/build.log"
    report 0
fi

if [ ! -x /app/lua ]; then
    echo "no executable ./lua after make" >>"$LOG/build.log"
    report 0
fi

echo "=== language purity (post-build) ===" >>"$LOG/purity.txt"
if ! python3 -I /tests/purity.py post /app >>"$LOG/purity.txt" 2>&1; then
    tail -5 "$LOG/purity.txt"
    report 0
fi

echo "=== MYCFLAGS pass-through ===" >>"$LOG/build.log"
if ! as_user "$BUILD_USER" \
        'cd /app && make clean && exec make MYCFLAGS=-DLUA_TASK_UNUSED_MACRO' \
        >>"$LOG/build.log" 2>&1; then
    echo "make does not honour MYCFLAGS" >>"$LOG/build.log"
    tail -20 "$LOG/build.log"
    report 0
fi
chmod -R a+rX /app 2>/dev/null || true
if ! as_user "$RUN_USER" 'cd /app && exec ./lua -v' >>"$LOG/build.log" 2>&1; then
    echo "./lua does not run after a MYCFLAGS build" >>"$LOG/build.log"
    tail -20 "$LOG/build.log"
    report 0
fi

echo "=== language purity (launcher depends on the build) ===" >>"$LOG/purity.txt"
if ! python3 -I /tests/purity.py depends /app >>"$LOG/purity.txt" 2>&1; then
    tail -5 "$LOG/purity.txt"
    report 0
fi

# Each graded program runs in a private directory of its own under here.
mkdir -p /run-corpus
chmod 1777 /run-corpus

# The corpus says which builds it wants: one for most milestones, one per
# compile-time compatibility option where the milestone has them.
rm -f "$LOG"/results-*.json
BUILDS=$(python3 -I - <<'PY'
import json, os
p = "/tests/corpus/builds.json"
try:
    with open(p) as f:
        bs = json.load(f)
except OSError:
    bs = [{"name": "default", "mycflags": ""}]
for b in bs:
    print("%s\t%s" % (b["name"], b.get("mycflags", "")))
PY
)
echo "$BUILDS" | while IFS=$'\t' read -r BNAME BFLAGS; do
    [ -n "$BNAME" ] || continue
    echo "=== build '$BNAME'  MYCFLAGS='$BFLAGS' ===" | tee -a "$LOG/build.log"
    chown -R "$BUILD_USER:$BUILD_USER" /app
    if ! as_user "$BUILD_USER" "cd /app && make clean && exec make MYCFLAGS='$BFLAGS'" \
            >>"$LOG/build.log" 2>&1; then
        echo "build '$BNAME' failed" | tee -a "$LOG/build.log"
        tail -20 "$LOG/build.log"
        continue
    fi
    # The build is over. The workspace goes back to root, readable by everyone
    # and writable by no one, so that a graded program cannot edit the
    # interpreter it is being graded through.
    chown -R root:root /app 2>/dev/null || true
    chmod -R a+rX /app 2>/dev/null || true
    python3 -I /tests/run_corpus.py --app /app --corpus /tests/corpus --build "$BNAME" \
        --out "$LOG/results-$BNAME.json" --jobs 7 --timeout 15 \
        --deadline-epoch "$((T0 + BUDGET))" 2>&1 | tee -a "$LOG/conformance.txt"
done

# Nothing of the candidate's may still be running while the score is worked out.
# The scorer below also stops reading anything relative to /app.
python3 -I /tests/sweep.py kill >>"$LOG/integrity.txt" 2>&1
cd / || report 0

FP_AFTER=$(python3 -I /tests/sweep.py fingerprint /tests)
echo "tests fingerprint after: $FP_AFTER" >> "$LOG/integrity.txt"
if [ "$FP_BEFORE" != "$FP_AFTER" ]; then
    echo "the grader's own files changed during the run" | tee -a "$LOG/integrity.txt"
    report 0
fi

echo "=== score ==="
SCORE=$(python3 -I /tests/score.py --corpus /tests/corpus --results "$LOG" \
        --out "$LOG/results.json" 2>&1 | tee -a "$LOG/conformance.txt" \
        | sed -n 's/^REWARD //p')
report "${SCORE:-0}"
}

# The whole grading run is bounded, not only its stages.
# task.toml gives this verifier 5400 s, and Harbor discards a step whose
# verifier overruns that -- reward file and all -- and then aborts every
# later step. BUDGET (4800 s) bounds what the candidate runs; this bounds
# everything else too (scans whose cost grows with what the build left in
# /app), at 5100 s, 300 s inside the limit. If it fires, every process of
# the candidate's uids is killed and the step reports 0.
if [ -z "${LUA_VERIFY_INNER:-}" ]; then
    LUA_VERIFY_INNER=1 timeout -k 30 5100 bash /tests/test.sh "$@"
    _rc=$?
    if [ "$_rc" = 124 ] || [ "$_rc" = 137 ]; then
        echo "grading run exceeded its 5100 s bound (rc=$_rc); reporting 0" >&2
        python3 -I /tests/sweep.py kill >>/logs/verifier/integrity.txt 2>&1
        report 0
    fi
    exit 0
fi
_verify "$@"
