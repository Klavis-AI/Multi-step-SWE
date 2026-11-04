#!/usr/bin/env bash

# --- verifier/step binding check -------------------------------------------
# /etc/harbor-step-id names the image; the literal below names this tests/ tree.
# They disagree only when the build context came from another step's cache
# entry, in which case every check below would grade the wrong milestone.
# Reported, not crashed: a non-zero exit makes the runner skip the remaining steps
# and buries the fault under a second symptom.
__hb_want=02
__hb_got=$(cat /etc/harbor-step-id 2>/dev/null) || __hb_got=MISSING
if [ "$__hb_got" != "$__hb_want" ]; then
    __hb_msg="VERIFIER/STEP MISMATCH: image is step ${__hb_got}, /tests tree is step ${__hb_want}."
    printf '%s\n' "$__hb_msg" >&2
    mkdir -p /logs/verifier 2>/dev/null
    printf '%s\n' "$__hb_msg" > /logs/verifier/step-mismatch.log
    # Numbers only. the runner parses every key under `rewards` as a float, and a
    # string field aborts the trial -- on the very path that exists to report a
    # fault. The names go to step-mismatch.log instead.
    printf '{"reward":0,"correctness":0,"geomean_speedup":1.0,"functions":0}\n' \
        > /logs/verifier/reward.json
    exit 0
fi
unset __hb_want __hb_got __hb_msg
# --- end binding check ------------------------------------------------------
set -u
source /tests/helpers.sh

# Lock the reward directory before anything else. the runner chmods /logs/verifier
# to 777 just before this container starts, so without this the candidate's own
# code -- which this step below both builds and runs -- could write the score.
# Every such site goes through run_unprivileged so that a 0700 root-owned
# directory is more than a mode: root would write it whatever its mode, an
# unprivileged process cannot. Nothing above this line runs candidate code.
harden_reward_dir

# Runs in this step's verifier container on a fresh copy of /app.
#
#   correctness : 1 only if the frozen set is restorable, the candidate tree
#                 builds, and the project's own auto-test mode reports no
#                 mismatch for this step's family or for the rest of the
#                 library.
#   speedup     : one ratio per scored function, read out of the project's own
#                 report: Base_ms (the scalar reference) over API_ms (the entry
#                 point the library dispatches to).  Both are timed by the same
#                 harness in the same process on the same input, so an
#                 unoptimized function reads exactly 1.0.
#   reward      : correctness * mean_f( ln(max(1, speedup_f)) )  [helpers.sh]
#
# Identical for every step. Per-step data: tests/FUNCS.step02,
# tests/FILTERS.step02, tests/GEOMETRY.step02.

SRC=/app/Simd
[ -d "$SRC/src/Simd" ] || { echo "missing $SRC/src/Simd"; report 0; }

echo "== assembling the graded tree =="
# The candidate owns src/Simd. The measuring instrument (src/Test), the scalar
# reference (src/Simd/SimdBase*) and the build system (prj/) are restored from
# the pinned release, so the instrument cannot be edited and the denominator
# cannot be slowed down. Nothing else is touched.
rm -rf /tmp/cand
cp -a "$SRC" /tmp/cand || { echo "cannot copy $SRC"; report 0; }
rm -rf /tmp/cand/build /tmp/cand/*/build
rm -rf /tmp/cand/src/Test /tmp/cand/prj
# Replace exactly the reference files the pin has, rather than every file whose
# name matches SimdBase*: a candidate that added a helper under that prefix is
# outside the contract and will fail to build, but the failure should come from
# its own code, not from the grader silently deleting the file.
(cd /pristine/src/Simd && ls SimdBase*.cpp SimdBase*.h 2>/dev/null) | while read -r f; do
    rm -f "/tmp/cand/src/Simd/$f"
done

if [ ! -d /pristine/src/Test ]; then
    echo "pristine reference tree missing (/pristine)"; report 0
fi
if [ ! -f /tests/.pristine-prepared ]; then
    echo "  preparing the pristine reference"
    # strip.py removes every ladder family's ISA files from a tree AND the
    # per-ISA autotest hooks that name their symbols. Running it on the
    # pristine reference therefore produces a test framework whose per-ISA
    # hooks are gone for all eight families but whose per-family Base hook --
    # which compares the *dispatched* entry point against the scalar reference,
    # bit for bit -- is untouched. That hook is the instrument this milestone
    # needs, and it is the only one that works for every implementation
    # strategy: a candidate that restores one ISA and not another would break a
    # per-ISA hook, and it is allowed to do exactly that.
    python3 /tests/vendor/strip.py /pristine \
        resizer blur edges cascade hog warp stats background \
        > /tmp/pristine_strip.log 2>&1 \
        || { echo "pristine strip failed"; tail -5 /tmp/pristine_strip.log; report 0; }
    # Assert the strip examined something. Its only evidence of work is the
    # line it prints, which would otherwise be read by nobody: a strip that
    # silently did nothing would leave the per-ISA autotest hooks in place, and
    # the next candidate to write one ISA and not another would fail to compile
    # and be scored 0 for a correct solution.
    removed=$(sed -n 's/.*removed \([0-9][0-9]*\) files.*/\1/p' /tmp/pristine_strip.log | tail -1)
    if [ "${removed:-0}" -lt 1 ]; then
        echo "  the pristine strip removed nothing (log below); the instrument would be inconsistent"
        tail -5 /tmp/pristine_strip.log | sed 's/^/    /'
        report 0
    fi
    echo "  pristine autotest hooks stripped for all eight families (${removed} files removed)"
    touch /tests/.pristine-prepared
fi
cp -a /pristine/src/Test /tmp/cand/src/Test || { echo "restore src/Test failed"; report 0; }
cp -a /pristine/prj /tmp/cand/prj || { echo "restore prj failed"; report 0; }
(cd /pristine/src/Simd && tar cf - SimdBase*.cpp SimdBase*.h) | (cd /tmp/cand/src/Simd && tar xf -) \
    || { echo "restore SimdBase failed"; report 0; }
echo "  frozen set restored: src/Test, src/Simd/SimdBase*, prj"

# Hand the graded tree and its build/run scratch to the unprivileged user.
# /app itself is never built here -- what is built is this copy -- so this is
# give_app_to_unprivileged's analogue for the tree that is actually compiled.
# Give the whole copy, not only the build dir: the configure step runs the
# tree's own CMake files and the source tree must stay writable by the build
# exactly as it is when this step runs as root.
mkdir -p /tmp/candbld /tmp/run /tmp/nb
chown -R "$SCORE_UID:$SCORE_GID" /tmp/cand /tmp/candbld /tmp/run /tmp/nb 2>/dev/null || true

# The graded input tree is candidate-writable and on the measured path: the
# framework reads data/ out of -r=/tmp/cand (the step-4 detector walks
# data/cascade/*.xml), so a candidate that edits it edits the input this step
# is scored on. Restore it from the pin, like the rest of the frozen set. Both
# copies come from the same pinned clone and the stripper touches only src/, so
# this replaces the tree's data/ with bytes identical to the ones shipped to
# the candidate -- it cannot move an honest measurement. It is restored after
# the chown above so it stays root-owned: readable by the graded run, not
# writable by it.
if [ ! -d /pristine/data ]; then
    echo "pristine input tree missing (/pristine/data)"; report 0
fi
rm -rf /tmp/cand/data
cp -a /pristine/data /tmp/cand/data || { echo "restore data failed"; report 0; }
echo "  graded input tree restored: data"

echo "== building candidate =="
build_candidate /tmp/cand /tmp/candbld || report 0

echo "== envelope: SSE4.1/AVX2 ceiling, scoped to what this step changed =="
# Out-of-envelope code is not a slower tree, it is a different machine, and the
# reward is not comparing that machine -- so this is a correctness gate, and it
# is scoped to the files this step owns.  A tree that changed nothing of this
# step's is reported as unchanged and skipped; the diff base is the pinned
# release with all eight families removed (the ladder's L0), the only base
# level this container has, so in the files this step shares with earlier
# steps the scanned lines also carry the candidate's own earlier work -- a
# line that is out of envelope there would already have failed the step that
# wrote it; the reference's own AVX-512 back ends are not compiled by this
# build, so they are ignored and that is asserted against the build's source
# list.  See vendor/envelope_scan.py for what a violation is.
correct=1
check_envelope /pristine /tmp/cand /tests/SCOPE.step02 /tests/ENVELOPE.step02 \
    /tmp/candbld || correct=0

mkdir -p /tmp/run
cd /tmp/run

echo "== correctness: this milestone's family, bit-exact against the scalar reference =="
# The project's auto-test mode compares every implementation against the entry
# point the library dispatches to, with differenceMax = 0 (exact equality), at
# several sizes and alignments, and stops the run on the first mismatch. A
# non-zero exit here is the milestone's own gate.
read -r GR_W GR_H GR_C GR_MT < /tests/GEOMETRY.step02
mapfile -t STEP_FILTERS < /tests/FILTERS.step02
for r in 1 2 3; do
    # The Test binary is linked against the candidate's library, so its static
    # initializers are candidate code: run it unprivileged.
    run_unprivileged /tests/vendor/run_test.sh /tmp/candbld/Test "$GR_W" "$GR_H" "$GR_C" "$GR_MT" \
        "/tmp/report_$r.txt" "${STEP_FILTERS[@]}" > "/tmp/step_run_$r.log" 2>&1
    rc=$?
    if [ "$rc" != 0 ] || [ ! -s "/tmp/report_$r.txt" ]; then
        echo "  graded run $r did not complete (exit $rc): a mismatch, a crash, or an empty report"
        tail -3 "/tmp/step_run_$r.log" | sed 's/^/    /'
        correct=0
    fi
done
[ -s /tmp/report_1.txt ] && echo "  graded reports written: $(ls /tmp/report_?.txt 2>/dev/null | wc -l) of 3"
correct=${correct:-1}

echo "== correctness: the whole library, no new failure =="
# Broad non-regression guard. Same instrument, every group in the project's
# suite, tiny performance geometry: correctness is what is being checked here,
# and it is checked relative to what the library does when nothing is
# optimized. Run only if the narrow gate passed -- a mismatching tree aborts
# this run at the same group anyway.
if [ "$correct" = 1 ]; then
    # Same binary, same reason as the graded runs: unprivileged.
    run_unprivileged /tests/vendor/run_test.sh /tmp/candbld/Test 320 240 8 1 /dev/null > /tmp/guard_run.log 2>&1
    guard_rc=$?
    # Assert it examined something: the suite prints this only when it ran
    # every selected group to the end without a single mismatch, so its presence
    # and its absence are both informative. "Zero failures over no tests" would
    # otherwise read exactly like a clean pass.
    if [ "$guard_rc" != 0 ] || ! grep -q 'ALL TESTS ARE FINISHED SUCCESSFULLY' /tmp/guard_run.log \
       || ! grep -q 'Test progress: 100.0%' /tmp/guard_run.log; then
        echo "  the project's own suite did not complete cleanly (exit $guard_rc):"
        tr '\r' '\n' < /tmp/guard_run.log | grep -E "Error|has errors|TERMINATED" | head -8 | sed 's/^/    /'
        correct=0
    else
        echo "  the whole suite ran to completion with no mismatch"
    fi
fi

echo "== per-function measurement =="
if [ "$correct" != 1 ]; then
    report 0
fi

mapfile -t FUNCS < /tests/FUNCS.step02
score_reports /tests/FUNCS.step02 /tmp/report_1.txt /tmp/report_2.txt /tmp/report_3.txt \
    > /tmp/speedups.tsv || { echo "  a scored function was never measured"; report 0; }
awk -F'\t' '{printf "  %-46s api=%9.3f ms base=%9.3f ms  %6.2fx\n", $1,$2,$3,$4}' /tmp/speedups.tsv
speedups=()
while IFS=$'\t' read -r name api base sp; do speedups+=("$sp"); done < /tmp/speedups.tsv

# Every scored function must have been measured in every run; an empty or short
# input set would otherwise read exactly like a clean pass.
if [ "${#speedups[@]}" -ne "${#FUNCS[@]}" ]; then
    echo "  measured ${#speedups[@]} of ${#FUNCS[@]} scored functions"
    report 0
fi
echo "  measured ${#speedups[@]} of ${#FUNCS[@]} scored functions, median over 3 runs"

report "$correct" "${speedups[@]}"
