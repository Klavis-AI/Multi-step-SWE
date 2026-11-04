#!/usr/bin/env bash
# Verifier for step 10 — PHP 8.3.
#
# Runs in this step's own container on a fresh copy of /app.  Everything it reads
# comes from /tests (this image), never from /app: the candidate's own copy of the
# test pool, of the runner and of the ini list is ignored.
set -u
source /tests/helpers.sh

# The provisional zero is written HERE, before the traps below are armed -- not only inside
# _verify.  A signal arriving between arming the trap and that later write would run the
# handler and exit 0, leaving no reward.json at all, and a step with no reward file is worse
# than a step scored zero: Harbor has nothing to record for it.  Writing it first makes the
# file exist for the whole life of the trap.  _verify rewrites the same bytes; harmless.
LOG=/logs/verifier
mkdir -p "$LOG"
printf '{"reward":0.000000}\n' > "$LOG/reward.json"

# Nothing below may resolve a module, a script or a relative path against /app: that is
# the candidate's tree, and Python puts the working directory on sys.path for a script
# read from stdin or given with -c.  Every interpreter below also runs with -I.
cd /

# Harbor discards a step's result -- reward file and all -- when its verifier outlives
# [steps.verifier] timeout_sec (3600 s), and then aborts every step after it, so a
# candidate that could stall this script would keep the mean of the steps before it.
# Every phase below has its own limit; this bounds the run as a whole as well.  The
# bounded copy is this same script, re-entered with an argument Harbor never passes.
# If it is cut short, report() kills everything it left running and scores the step 0.
# Worst case: 3300 s + 30 s kill grace + the sweep, inside 3600 s.
if [ "${1:-}" != "--bounded" ]; then
    timeout -k 30 3300 bash /tests/test.sh --bounded
    rc=$?
    if [ "$rc" = 124 ] || [ "$rc" = 137 ]; then
        echo "the verifier did not finish within its 3300 s bound; reporting 0" \
            | tee -a "$LOG/score-anomaly.txt"
        report 0
    fi
    exit 0
fi

# The held-out material must not survive this verifier by ANY exit path.  This package
# grades in separate mode, in a container of its own, so what follows is defence in
# depth: in shared mode -- the verifier running inside the agent's own container --
# Harbor empties /tests only immediately before the NEXT verifier, so anything still
# there is readable by the agent for the whole of the next milestone's turn.  A removal
# written at the end of the happy path does not do that job: every `report 0` above it (the copy
# failed, a source/payload/binary gate tripped, make produced no binary, the canary did not
# fail) exits first and skips it -- and a broken build is precisely the case where reading
# the next step's scored list would pay.  A trap is attached to the exit itself, so no
# branch can route around it; `report` always exits 0, and this changes neither the reward
# nor the exit status.
_cleanup_heldout() {
    rm -rf /tests/pool /tests/duals /tests/current.txt /tests/regression.txt \
           /tests/duals.txt /tests/php-src-hashes.txt /tests/php-src-fingerprints.bin \
           2>/dev/null || true
}
trap _cleanup_heldout EXIT
trap '_cleanup_heldout; exit 0' HUP INT TERM

# bash reads a script INCREMENTALLY as it executes, so if this file were writable by the
# thing it is grading, rewriting its tail mid-run would change what happens after the score
# is decided -- and the guard, the canary, the scoring and the final write all live in
# the tail.  Wrapping the body in a function forces bash to parse through to
# the closing brace before executing any of it.  /tests is not writable by the uid that runs
# candidate code and the guard below measures that every run; this makes the parse order not
# depend on that holding.
_verify() {
LOG=/logs/verifier
mkdir -p "$LOG"

# A provisional zero, overwritten by the real score.  If this verifier is killed before
# it can report -- the kernel taking the container, a timeout -- Harbor finds a reward
# file and scores the step 0 instead of aborting every step after it.  One dead verifier
# should cost one milestone, not the rest of the chain.
printf '{"reward":0.000000}\n' > "$LOG/reward.json"
BUILD=/build

echo "=== step 10 (PHP 8.3) verifier ==="

# ---------------------------------------------------------------- 1. rebuild
# Copy the candidate tree and drop every build product, so the binary that is
# graded is built here from the sources that were submitted.  The corpus and the
# release notes are left out of the copy: the build must not depend on them.
rm -rf "$BUILD"
mkdir -p "$BUILD"
if ! (cd /app && tar cf - \
        --exclude=./.git --exclude='*.o' --exclude='*.a' --exclude='*.so' \
        --exclude=./setup.sh \
        --exclude='*.lo' --exclude='*.la' --exclude=./tests --exclude=./SPEC . ) \
     | (cd "$BUILD" && tar xf -) ; then
    echo "could not copy /app" | tee -a "$LOG/build.log"
    report 0
fi
# Delete anything that is already an executable image.
find "$BUILD" -type f -size +3c -print0 2>/dev/null | while IFS= read -r -d '' f; do
    if [ "$(head -c 4 "$f" | od -An -c | tr -d ' \n')" = "177ELF" ]; then rm -f "$f"; fi
done

# Record what the candidate actually submitted.  A reward of 0 has several very
# different causes -- nothing written yet, a build that fails, a gate that trips, a
# workspace that never arrived -- and this is what tells them apart afterwards without
# having to infer it from the agent's transcript.  ASCII only, because the log directory
# is carried back by a transport that desynchronises on any byte >= 0x80.
{
    echo "files=$(find "$BUILD" -type f 2>/dev/null | wc -l) bytes=$(du -sb "$BUILD" 2>/dev/null | cut -f1)"
    echo "makefile=$(cd "$BUILD" 2>/dev/null && ls 2>/dev/null | grep -ixc 'makefile' || echo 0)"
    echo "--- largest 200 files: size, path ---"
    find "$BUILD" -type f -printf '%10s  %P\n' 2>/dev/null | sort -rn | head -200
} 2>&1 | tr -c '[:print:]\n' '?' > "$LOG/workspace-inventory.txt" || true

# ------------------------------------------ 1b. protect the answers, before any
# candidate code runs.  Everything that executes candidate input from here on -- the
# build and the binary under test -- runs as $RUNAS, so the expectations must be
# unreadable to that uid before the first of them starts.  Without this a "PHP
# interpreter" that reads the .phpt for the script it was handed and prints its
# --EXPECT-- section scores over half of this step while implementing no PHP at all.
RUNAS=65534      # nobody: nothing running candidate code may read the answers
chmod -R go-rwx /tests 2>/dev/null || true
chmod -R go-rwx /app/tests /app/SPEC 2>/dev/null || true

# Root runs several /tests scripts again AFTER candidate code has run (the build, the
# binary under test): the gates, the runner, the scorer and the redactor.  It runs a
# root-only copy of each, taken here, before any candidate code, and before every such run
# re-checks that the copy is unchanged and that /tests is still root's and not writable by
# group or other.  If either has changed, the grading is not trustworthy and scores 0.
SEALED="run-phpt.py score.py redact.py srcgate.py bingate.py"
SEAL=$(mktemp -d /root/php-verifier-seal.XXXXXX) || report 0
for s in $SEALED; do
    cp -- "/tests/$s" "$SEAL/$s" || report 0
done
chmod 0700 "$SEAL" && chmod 0600 "$SEAL"/*.py || report 0
SEAL_SUM=$(cd "$SEAL" && sha256sum -- $SEALED) || report 0
_sealed_ok () {
    local out
    [ -d "$SEAL" ] && [ ! -L "$SEAL" ] || return 1
    [ "$(stat -c '%u %a' -- "$SEAL")" = "0 700" ] || return 1
    out=$(find "$SEAL" -mindepth 1 \( ! -type f -o ! -user 0 -o -perm /022 \) -print -quit) || return 1
    [ -z "$out" ] || return 1
    [ "$(cd "$SEAL" && sha256sum -- $SEALED)" = "$SEAL_SUM" ] || return 1
    [ -d /tests ] && [ ! -L /tests ] || return 1
    out=$(find /tests \( ! -user 0 -o \( ! -type l -perm /022 \) \) -print -quit) || return 1
    [ -z "$out" ]
}
_require_sealed () {
    _sealed_ok && return 0
    echo "the verifier's scripts or /tests changed, or became writable by others, after" \
         "candidate code ran (checked before: $1); reporting 0" | tee -a "$LOG/score-anomaly.txt"
    report 0
}

# The reward file is the single output that decides the score, and the graded binary runs
# INSIDE this container while the scorer runs.  If that binary could write /logs/verifier,
# a detached child looping a write would outlive the scorer and simply win -- "the harness
# writes last" is an ordering, and orderings lose races.  Make the write impossible rather
# than early: the directory stays root-owned and not group/other writable.
chown root:root /logs/verifier 2>/dev/null || true
chmod 0755 /logs /logs/verifier 2>/dev/null || true

# Assert the protection actually took, BEFORE the build.  A chmod that silently fails
# -- a read-only mount, an unexpected owner -- would leave the expectations readable
# and the grading compromised, with nothing in the log to say so.
python3 -I - "$RUNAS" > "$LOG/answerkey-guard.txt" 2>&1 <<'GUARD'
import glob, os, subprocess, sys
uid = int(sys.argv[1])
probes = []
for root in ("/tests/pool", "/tests/duals", "/app/tests/phpt"):
    hit = glob.glob(os.path.join(root, "**", "*.phpt"), recursive=True)
    if hit:
        probes.append(hit[0])
for extra in ("/tests/current.txt", "/tests/duals.txt", "/tests/regression.txt"):
    if os.path.exists(extra):
        probes.append(extra)
if not probes:
    print("answer-key guard: nothing to protect")
    sys.exit(0)
bad = []
for p in probes:
    try:
        r = subprocess.run(["head", "-c", "1", p], user=uid, group=uid,
                           capture_output=True, timeout=30)
        if r.returncode == 0:
            bad.append(p)
    except Exception:
        pass                      # cannot even spawn as that uid: it cannot read either
if bad:
    print("WARNING: expectations are READABLE by uid %d -- this grading is NOT protected:" % uid)
    for p in bad:
        print("   " + p)
else:
    print("answer-key guard: uid %d cannot read any of %d expectation paths" % (uid, len(probes)))

# Reading the answers is only half of it.  The graded binary runs as this uid inside the
# verifier's own container, concurrently with the scorer, so if it can WRITE the reward
# file it can decide its own score by outliving the scorer.  Assert it cannot -- and
# assert it every run, so this is a recorded negative rather than an inference from
# whatever mode the base image happened to create /logs with.
writable = []
for target in ("/logs/verifier", "/logs", "/tests"):
    probe = os.path.join(target, ".write-probe")
    try:
        r = subprocess.run(["sh", "-c", "printf x > '%s' && echo YES" % probe],
                           user=uid, group=uid, capture_output=True, timeout=30)
        if b"YES" in r.stdout:
            writable.append(target)
    except Exception:
        pass
    finally:
        try:
            os.unlink(probe)
        except OSError:
            pass
if writable:
    print("WARNING: uid %d can WRITE %s -- the graded process can overwrite its own reward "
          "file, so this grading is NOT trustworthy" % (uid, ", ".join(writable)))
else:
    print("reward guard: uid %d cannot write /logs/verifier, /logs or /tests" % uid)
GUARD
cat "$LOG/answerkey-guard.txt"

# ------------------------------------------- 2. the sources must be the candidate's
# srcgate_on <log> <tree>...: php-src sources, verbatim or edited, fail the step.  Bounded;
# a tree too large to examine in 600 s fails closed rather than going unexamined.
srcgate_on () {
    local out="$1" t rc=0
    shift
    _require_sealed srcgate
    : > "$out"
    for t in "$@"; do
        [ -d "$t" ] || continue
        echo "--- $t" >> "$out"
        timeout -k 10 600 python3 -I "$SEAL/srcgate.py" "$t" /tests/php-src-hashes.txt \
            /tests/php-src-fingerprints.bin >> "$out" 2>&1
        rc=$?
        [ "$rc" = 124 ] && echo "source gate did not finish within 600 s" >> "$out"
        [ "$rc" != 0 ] && return 1
    done
    return 0
}
if ! srcgate_on "$LOG/srcgate.txt" "$BUILD"; then
    cat "$LOG/srcgate.txt"
    echo "php-src sources, verbatim or edited, are not accepted" >> "$LOG/srcgate.txt"
    report 0
fi
if ! timeout -k 10 300 python3 -I "$SEAL/bingate.py" --payload "$BUILD" > "$LOG/payloadgate.txt" 2>&1; then
    cat "$LOG/payloadgate.txt"
    report 0
fi

# ------------------------------------------------------------------ 3. build
# A Makefile is a program the candidate wrote, so the build is candidate code and
# gets the same treatment as the binary: an unprivileged uid and an environment
# built from scratch.  Run as root it could read /tests -- the scored list, the
# pool, the hidden duals -- and copy them into $BUILD, which section 4 then makes
# world-readable for the binary under test.  Locking the binary out of the answers
# is worthless if the build can carry them across first.
echo "--- make ---"
chown -R "$RUNAS":"$RUNAS" "$BUILD" 2>/dev/null || true
timeout -k 30 660 python3 -I - "$BUILD" "$RUNAS" > "$LOG/build.log" 2>&1 <<'BUILD_EOF'
import subprocess, sys
build, uid = sys.argv[1], int(sys.argv[2])
env = {
    "PATH": "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
    "HOME": build, "TMPDIR": build, "TEMP": build,
    "LC_ALL": "C", "LANG": "C", "TZ": "UTC", "MAKEFLAGS": "",
}
try:
    r = subprocess.run(["make"], cwd=build, env=env, user=uid, group=uid,
                       extra_groups=[], timeout=600)
    sys.exit(r.returncode)
except subprocess.TimeoutExpired:
    print("make exceeded 600s")
    sys.exit(124)
except OSError as exc:
    print("cannot start make: %s" % exc)
    sys.exit(125)
BUILD_EOF
BUILD_RC=$?
tail -n 40 "$LOG/build.log"
# Nothing the build started may still be running while the binary is checked and graded,
# and the tree it built is root's from here on: what is judged is what make left behind.
_reap_uid "$RUNAS"
chown -R root:root "$BUILD" 2>/dev/null || true
# The build is candidate code with the network available.  The source gate ran on what was
# submitted; run it again on everything the build could write, so php-src fetched or
# unpacked at build time and left on disk is judged like php-src submitted.
if ! srcgate_on "$LOG/srcgate-postbuild.txt" "$BUILD" /tmp /var/tmp /dev/shm; then
    cat "$LOG/srcgate-postbuild.txt"
    echo "the build produced php-src sources, verbatim or edited" >> "$LOG/srcgate-postbuild.txt"
    report 0
fi
PHPBIN="$BUILD/sapi/cli/php"
if [ ! -f "$PHPBIN" ] || [ ! -x "$PHPBIN" ]; then
    echo "make did not produce an executable at sapi/cli/php (make exit $BUILD_RC)"
    report 0
fi
if [ "$(head -c 4 "$PHPBIN" | od -An -c | tr -d ' \n')" != "177ELF" ]; then
    echo "sapi/cli/php is not a native executable"
    report 0
fi
_require_sealed bingate
if ! timeout -k 10 330 python3 -I "$SEAL/bingate.py" --binary "$BUILD" "$PHPBIN" > "$LOG/bingate.txt" 2>&1; then
    cat "$LOG/bingate.txt"
    report 0
fi
cat "$LOG/bingate.txt"

# --------------------------------------------------------------- 4. run tests
POOL=/tests/pool
DUALS=/tests/duals
INI=/tests/run-tests-ini.txt
JOBS=7
TMO=20
MEMCAP=2048      # ceiling on one test's resident memory, in MB

chmod -R a+rX "$BUILD" 2>/dev/null || true


run_set () {   # run_set <name> <listfile> <root> <cap-seconds>
    local name="$1" list="$2" root="$3" cap="$4"
    if [ ! -s "$list" ]; then return 0; fi
    echo "--- $name ($(grep -c . "$list") tests) ---"
    _require_sealed "$name"
    timeout -k 30 "$cap" python3 -I "$SEAL/run-phpt.py" --php "$PHPBIN" --root "$root" \
        --ini "$INI" --list "$list" -j "$JOBS" --timeout "$TMO" \
        --memory-cap-mb "$MEMCAP" --run-as "$RUNAS" \
        --json "$LOG/$name.json" --quiet > "$LOG/$name.log" 2>&1
    tail -n 3 "$LOG/$name.log"
    _reap_uid "$RUNAS"   # one set's leftovers must not run on into the next
}

# Before anything is scored, prove the measurement is still live.  This canary's
# expectation is text no correct interpreter can print, so it MUST fail.  If it passes,
# the comparison is not comparing -- a broken EXPECTF conversion, a runner that reports
# success on an empty read, an interpreter that has made the harness agree with it --
# and every other number this run produces is meaningless.  A dead measurement must not
# be able to report a passing score.
_require_sealed canary
mkdir -p /tests/canary && cp /tests/canary.phpt /tests/canary/canary.phpt 2>/dev/null
echo "canary.phpt" > /tests/canary.txt
chmod -R go-rwx /tests/canary /tests/canary.txt 2>/dev/null || true
timeout -k 10 120 python3 -I "$SEAL/run-phpt.py" --php "$PHPBIN" --root /tests/canary \
    --ini "$INI" --list /tests/canary.txt -j 1 --timeout "$TMO" \
    --memory-cap-mb "$MEMCAP" --run-as "$RUNAS" \
    --json "$LOG/canary.json" --quiet > "$LOG/canary.log" 2>&1
_reap_uid "$RUNAS"
CANARY=$(python3 -I -c "
import json,sys
try: d=json.load(open('$LOG/canary.json'))
except Exception: print('NO-REPORT'); sys.exit()
rs=d.get('results',[])
print(rs[0]['status'] if len(rs)==1 else 'MALFORMED')
")
# The canary is healthy only if it FAILED for the right reason.  Anything else --
# PASS (the comparison is not comparing), BORK (the runner could not process it),
# TIMEOUT, a missing report -- means the measurement did not demonstrate that it
# works, and an undemonstrated measurement must not be allowed to report a score.
# Accepting "not PASS" as healthy would be the same mistake in miniature: it would
# report "the comparison is live" on a BORK.
case "$CANARY" in
    FAIL)
        echo "canary: failed as required, the comparison is live" > "$LOG/canary-verdict.txt" ;;
    TIMEOUT)
        # The candidate did not answer in time.  That is a fact about the candidate, not
        # about the comparison -- the measurement is unproven here but not disproven, and
        # blaming the grader for a slow interpreter would mis-attribute the whole run.
        # Score normally; a candidate that times out everywhere scores badly on its merits.
        echo "canary: TIMEOUT -- the candidate did not answer within ${TMO}s; the comparison is unproven for this run but the fault is the candidate's, so scoring continues" \
            | tee "$LOG/canary-verdict.txt" ;;
    *)
        # PASS  -> the comparison accepted an expectation no correct interpreter produces.
        # BORK  -> the runner could not process the canary at all.
        # Either way the instrument has not shown it can detect a wrong answer.
        echo "CANARY $CANARY -- expected FAIL; the measurement is not demonstrably live, so this grading is not meaningful" \
            | tee "$LOG/canary-verdict.txt"
        report 0 ;;
esac

run_set current    /tests/current.txt    "$POOL"  950
run_set regression /tests/regression.txt "$POOL"  600
run_set duals      /tests/duals.txt      "$DUALS" 600

# ------------------------------------------------------------------ 5. score
_require_sealed score
timeout -k 10 60 python3 -I "$SEAL/score.py" \
    current=0.45:"$LOG/current.json":/tests/current.txt:T1 \
    regression=0.20:"$LOG/regression.json":/tests/regression.txt:T2 \
    duals=0.35:"$LOG/duals.json":/tests/duals.txt:T1
if [ -s "$LOG/reward.value" ]; then
    REWARD=$(cat "$LOG/reward.value")
else
    echo "SCORE NOT COMPUTED: score.py wrote no reward.value; reporting 0 -- this is a" \
         "verifier failure, not a candidate score" | tee -a "$LOG/score-anomaly.txt"
    REWARD=0
fi

# The hidden material must not survive into the next milestone's session.  Harbor
# empties /logs/verifier and /tests only immediately before the NEXT verifier, so in
# shared mode whatever is left here is readable by the agent for the whole of the
# next step's turn.  The scored lists and the pools go; the per-test reports keep their
# statuses but have the test paths replaced by their index in the list, which identifies
# nothing to someone who has never seen the list.
_require_sealed redact
timeout -k 10 60 python3 -I "$SEAL/redact.py" "$LOG/current.json"    /tests/current.txt    2>/dev/null || true
timeout -k 10 60 python3 -I "$SEAL/redact.py" "$LOG/regression.json" /tests/regression.txt 2>/dev/null || true
timeout -k 10 60 python3 -I "$SEAL/redact.py" "$LOG/duals.json"      /tests/duals.txt      2>/dev/null || true

# Redundant with the EXIT trap armed at the top of this file, and deliberately so: the
# trap is what makes the removal unskippable, this call is what keeps it visible at the
# point in the flow where a reader looks for it.  Same function, and rm -rf is idempotent.
_cleanup_heldout

report "$REWARD"
}
_verify "$@"

# --- packaging guard: unique file SIZE -------------------------------
# These eleven sibling files are generated from one template and differ only in
# this step's label and scoring weights.  Docker's build-context sync treats
# (path, size, mtime) as identity, so same-path same-size siblings can be served
# from a previous build's cached copy -- delivering another step's script here.
# Only a DIFFERENT SIZE prevents that.  mtime does NOT: git checkout stamps every
# file with one time, and GNU tar stores mtime to the second, so any spacing the
# working tree had is gone the moment this package is cloned or tarred.  The pad
# below therefore exists solely to keep this file's size unique among its
# siblings.  Do not normalise it, and do not rely on mtime instead.
# step 10-php-8-3 size-pad --------------------------------------------------------------------------------
