#!/usr/bin/env bash
set -u
# A verifier that dies without writing a reward takes every later step of the
# chain down with it, so the file exists before anything can kill us -- before
# helpers.sh is even sourced -- and the real score overwrites it.
mkdir -p /logs/verifier
printf '{"reward":0.000000}\n' > /logs/verifier/reward.json
source /tests/helpers.sh

log() { echo "[verify] $*"; }
harden_reward_dir

# ------------------------------------------------------- whole-run bound
# Harbor discards a step whose verifier outlives [steps.verifier] timeout_sec
# (3,600 s) even when reward.json exists, and then aborts every later step, so a
# workspace that can make this script hang keeps the mean of the steps before it.
# Each candidate invocation below has its own timeout; this bounds their sum and
# everything they do not cover (find/cp/chown over a tree the candidate sized).
# Worst case: 3,300 s + 30 s kill grace + the sweep, under 3,600 - 240.
if [ "${1:-}" != "--bounded" ]; then
    timeout -k 30 3300 bash /tests/test.sh --bounded
    rc=$?
    if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
        log "verifier run exceeded its 3300 s bound (exit $rc); this step scores 0"
        report 0
    fi
    sweep_candidate
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    exit 0
fi

# Scorer intermediates live where only root can write. The candidate uid can
# create files in /tmp, and a log it created before the scorer opened it by name
# is a log it can still rewrite after the scorer has written it.
W=$(mktemp -d)
# The decoy launchers (Dockerfile) append to a root-owned file in a directory
# only root can change: the candidate uid can write it, but cannot remove or
# replace it, and any write -- truncation included -- moves its ctime.
DECOY0=$(decoy_stamp)
if [ "$DECOY0" = "missing" ]; then
    log "the decoy marker is missing from this image"
    report 0
fi

if ! strip_setid /app; then
    log "a set-user-ID or set-group-ID file under /app could not be cleared"
    report 0
fi

# Built from source or it does not exist.  The candidate's workspace reaches
# this image as text on two paths -- /app/jvm/src and /app/jvm/Makefile -- so
# everything else under /app/jvm is a build product, and all of it goes before
# the platform library is restored.
find /app/jvm -mindepth 1 -maxdepth 1 ! -name src ! -name Makefile -exec rm -rf {} +
find /app/jvm/src \( -name '*.o' -o -name '*.a' -o -name '*.so' -o -name '*.class' \) -delete

# The scorer always works from its own copies: the platform class library and
# the corpus in this image, never the ones in the candidate workspace.
mkdir -p /app/jvm/lib
cp -r /tests/platform /app/jvm/lib/platform

if [ ! -f /app/jvm/Makefile ]; then
    log "no /app/jvm/Makefile"
    report 0
fi

# The makefile is the candidate's, so `make` is candidate-authored code. Run as
# root it can rewrite this scorer before this scorer runs -- /tests is root-owned
# and root-writable -- so the build gets the same unprivileged uid the tests do.
# The tree it builds in becomes the candidate's; /tests does not.
chown -R 65534:65534 /app/jvm 2>/dev/null
if [ "$(id -u)" = "0" ] && [ -x /usr/bin/setpriv ]; then
    BUILD_AS="/usr/bin/setpriv --reuid=65534 --regid=65534 --clear-groups --no-new-privs --"
else
    BUILD_AS=""
    log "no privilege drop available for the build"
fi
( cd /app && HOME=/tmp timeout -k 30 600 $BUILD_AS make -C /app/jvm ) > "$W/build.log" 2>&1
build_rc=$?
# whatever the build left running goes before anything reads what it built
sweep_candidate
tail -40 "$W/build.log"
if [ $build_rc -ne 0 ] || [ ! -x /app/jvm/bin/java ]; then
    log "build failed (rc=$build_rc)"
    report 0
fi

# The candidate is run by an unprivileged uid, so what it needs must be legible
# to one: the launcher it just built, and the platform class library.
# whatever the makefile decided to produce, wherever it put it
chmod -R a+rX /app/jvm 2>/dev/null

# A launcher that merely wraps another runtime is not the artifact.  Read the
# dynamic section rather than running `ldd`: ldd executes the binary's own
# PT_INTERP, so a provenance check that looks static would run the candidate's
# code, as root, before the sandbox is anywhere near it.
if readelf -d /app/jvm/bin/java 2>/dev/null | grep -qE 'libjvm|libjava|libjli'; then
    log "launcher links against a JVM library"
    report 0
fi
if ! head -c 4 /app/jvm/bin/java | od -An -c | grep -q '177   E   L   F'; then
    log "launcher is not an ELF executable"
    report 0
fi
nc=$(find /app/jvm -name '*.c' | wc -l)
if [ "$nc" -lt 1 ]; then
    log "no C sources under /app/jvm"
    report 0
fi

# The corpus roots are read out of this image instead of being named here.
# Each step's verifier image holds exactly its own conformance set and, from
# rung 2 on, its regression set -- the root whose name ends in `r`, which sorts
# second, which is the order the scorer's own x regression product expects.  Deriving
# them keeps one script correct on every rung: these six files are generated
# from one template and differ by a two-digit number, and a builder that decides
# a context file is unchanged from (path, size, mtime) will hand one rung's copy
# to another.  When that happens the scorer runs the corpus that is actually
# in the image rather than dying on a root that is not.  The denominator below
# is already derived from the image the same way.
CORPUS_ROOTS=$(find /tests/corpus -mindepth 1 -maxdepth 1 -type d | sort | tr '\n' ' ')
if [ -z "$CORPUS_ROOTS" ]; then
    log "no corpus directories in this image"
    report 0
fi
log "rung 4 of 6 (04-classfile-56-61); corpus roots: $CORPUS_ROOTS"
# from /, isolated: the scorer must not import anything from a directory or
# environment the candidate could have shaped
( cd / && timeout -k 30 1500 python3 -I /tests/run_corpus.py 90 $CORPUS_ROOTS ) > "$W/corpus.log" 2>&1
corpus_rc=$?
sweep_candidate
if [ $corpus_rc -ne 0 ] || ! grep -q '^SCORE ' "$W/corpus.log"; then
    log "corpus runner did not finish (rc=$corpus_rc) -- this is a zero because"
    log "the scorer stopped, not because the candidate failed every case"
    tail -20 "$W/corpus.log"
fi
cat "$W/corpus.log"

if [ "$(decoy_stamp)" != "$DECOY0" ]; then
    log "artifact invoked an external JVM (the decoy marker changed)"
    report 0
fi
# Another Java runtime has to be on disk while a case runs on it: run_corpus.py
# inspects every directory the candidate uid can write before, during and after
# the cases (foreign.py), and says so in one SCAN line.
if grep -q '^FOREIGN ' "$W/corpus.log"; then
    log "another Java runtime was on disk where the candidate can write"
    report 0
fi
if grep -q '^SCORE ' "$W/corpus.log" && ! grep -q '^SCAN ok ' "$W/corpus.log"; then
    log "the scan for another Java runtime did not complete"
    report 0
fi

if grep -q '^GUARDS LEAK' "$W/corpus.log"; then
    log "the scored answers are readable by the candidate -- this score would mean nothing"
    report 0
fi
if ! grep -q '^SCORE ' "$W/corpus.log"; then
    log "no corpus result"
    report 0
fi
# The denominator comes from the corpus in this image, never from the output of
# a run the candidate took part in: a crashed launcher, a timeout and a wrong
# answer all have to land as a failed case rather than as a smaller total.
want=$(find /tests/corpus -mindepth 2 -maxdepth 2 -type d | wc -l)
got=$(grep '^SCORE ' "$W/corpus.log" | tail -1 | awk '{print $3}')
if [ "${got:-0}" -ne "$want" ]; then
    log "the runner reported ${got:-0} cases and the image holds $want"
    report 0
fi
# The scorer's own arithmetic is pinned at both ends before any real number is
# believed, and the weighting itself lives in helpers.sh so every step shares
# one implementation.
if ! score_control 0; then
    log "the scorer does not give 1.000000 for all-correct and 0.000000 for none"
    report 0
fi
reward=$(score_from "$W/corpus.log")
report "$reward"

# Size pad.  Docker decides a build-context file is unchanged from
# (path, size, mtime), and these six scripts come from one template: without a
# per-rung difference in length they collide, and a colliding sibling's copy is
# delivered silently.  One dash per rung, an independent guard sitting on top of
# the derived corpus roots above: ----
