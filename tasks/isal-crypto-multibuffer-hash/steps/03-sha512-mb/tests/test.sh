#!/usr/bin/env bash

# --- verifier/step binding check -------------------------------------------
# /etc/harbor-step-id names this image. BuildKit identifies a build-context
# file by (relative path, size, mtime) and never reads its bytes, so two steps
# whose tests/ trees share a filename, a size and a second are indistinguishable
# to it -- and the wrong milestone's expectations get baked in at an
# ordinary-looking score. Nothing inside a Dockerfile prevents that. This file
# is byte-identical in every step and carries no step literal, so it cannot
# itself be substituted; the per-step files it needs are named after the step
# and bound by digest, so a substitution is loud.
#
# Reported, not crashed: a non-zero exit makes Harbor skip the remaining steps
# and buries the fault under a second symptom.
#
# The directory variables below have defaults matching the real container and
# exist so the whole verifier can be exercised outside it (see the task README).
STEP_ID_FILE=${STEP_ID_FILE:-/etc/harbor-step-id}
TESTS_DIR=${TESTS_DIR:-/tests}
APP_TREE=${APP_TREE:-/app/isal_crypto}
BASE_TREE=${BASE_TREE:-/base}
REWARD_DIR=${REWARD_DIR:-/logs/verifier}
BASE_RAN=${BASE_RAN:-/base_check_ran.txt}
BASE_FAILURES=${BASE_FAILURES:-/base_check_failures.txt}
# Where the two digest streams are captured.  Root-owned and 0700: the streams
# are written by this shell's redirection, not by the harness process, so the
# bytes that get compared are never bytes a candidate-owned process can write.
DUMP_DIR=${DUMP_DIR:-/tmp/verifier-dumps}

__hb_want=$(cat "$STEP_ID_FILE" 2>/dev/null) || __hb_want=MISSING
__hb_why=""
case "$__hb_want" in
    [0-9][0-9]) ;;
    *) __hb_why="image carries no step id" ;;
esac
if [ -z "$__hb_why" ] && [ ! -f "$TESTS_DIR/FUNCS.step$__hb_want" ]; then
    __hb_why="no FUNCS.step$__hb_want in this build context"
fi
if [ -z "$__hb_why" ] && [ ! -f "$TESTS_DIR/harness/bench.step$__hb_want.c" ]; then
    __hb_why="no harness/bench.step$__hb_want.c in this build context"
fi
if [ -z "$__hb_why" ] && ! (cd "$TESTS_DIR" && sha256sum -c --quiet \
        "BINDING.step$__hb_want") > /dev/null 2>&1; then
    __hb_why="per-step files do not match BINDING.step$__hb_want"
fi
if [ -n "$__hb_why" ]; then
    __hb_msg="VERIFIER/STEP MISMATCH: image says step ${__hb_want} (${__hb_why})."
    printf '%s\n' "$__hb_msg" >&2
    mkdir -p "$REWARD_DIR" 2>/dev/null
    printf '%s\n' "$__hb_msg" > "$REWARD_DIR/step-mismatch.log"
    printf '{"reward":0,"verifier_error":"step_mismatch","image_step":"%s"}\n' \
        "$__hb_want" > "$REWARD_DIR/reward.json"
    exit 0
fi
unset __hb_want __hb_why __hb_msg
# --- end binding check ------------------------------------------------------
set -u
source "$TESTS_DIR/helpers.sh"
if [ "$REWARD_DIR" = /logs/verifier ]; then
    harden_reward_dir
else
    mkdir -p "$REWARD_DIR"
fi

# Runs in this step's verifier container on a fresh copy of /app.
#
#   correctness : 1 only if the candidate builds, every scored unit reproduces
#                 the base build's output byte for byte, the project's own
#                 suite gains no new failure, no unit regressed against the
#                 base, and -- when the candidate changed files this step owns
#                 -- that code stays inside the capability envelope this host
#                 executes (tests/check_envelope.sh).
#   speedup     : one ratio per scored unit, base-ns over candidate-ns, from a
#                 harness that drives the PUBLIC entry points of both trees --
#                 so what is timed is whatever implementation each installed.
#   reward      : correctness * mean_u( ln(max(1, speedup_u)) )   [helpers.sh]
#
# Identical in every step. Per-step data: FUNCS.stepNN (the scored unit ids),
# STAGE.stepNN (the stage directories), harness/bench.stepNN.c.

STEP=$(cat "$STEP_ID_FILE")
STAGE_DIRS=$(cat "$TESTS_DIR/STAGE.step$STEP")
FUNCS="$TESTS_DIR/FUNCS.step$STEP"
CAND=$APP_TREE
BASE=$BASE_TREE

# Timing. The two sides are measured alternately rather than one after the
# other: this host is shared, and its throughput drifts by more than the
# thinnest step's whole effect over a few minutes. Interleaving a short run of
# each side and taking the median of the per-round ratios cancels the drift;
# the harness's own median over its internal repetitions cancels the
# occasional outliers within a round.
NROUNDS=5
NREPS_PER_ROUND=3

# The one line a bench run is allowed to print (bench_common.h, bc_report):
# unit id, median ns, the per-repetition samples, the timed path's output
# fingerprint and its word count.  test.sh refuses to score a run whose stdout
# is anything else -- the candidate's library shares that stream.
REPORT_LINE='^[A-Za-z0-9_]+ [0-9]+( [0-9]+)* fp=[0-9a-f]+ fpn=[0-9]+$'

[ -d "$CAND" ] || { echo "missing $CAND"; report 0; }
[ -s "$FUNCS" ] || { echo "no scored units for this step"; report 0; }
[ -d "$BASE" ] || { echo "missing base tree $BASE"; report 0; }

# The digest streams compared below are captured through a path the harness
# process cannot write itself (see the dump runs), so the directory has to
# exist and be this shell's alone before anything candidate-derived runs.
mkdir -p "$DUMP_DIR"
chmod 700 "$DUMP_DIR"

# The candidate tree's own make runs recipes from files the candidate
# wrote, so the tree is handed to the unprivileged identity before the
# first of them runs (helpers.sh, give_app_to_unprivileged): root in this
# container writes the reward directory whatever its mode, and a recipe
# is candidate code.
give_app_to_unprivileged "$CAND"

echo "== building candidate =="
if ! build_tree "$CAND"; then
    echo "candidate build failed"; tail -30 /tmp/build.log; report 0
fi
echo "  built $CAND/bin/isa-l_crypto.a"

correct=1

echo "== harness =="
if ! build_harness "$BASE" /tmp/h_base; then
    echo "base harness build failed"; tail -20 /tmp/harness.log; report 0
fi
if ! build_harness "$CAND" /tmp/h_cand; then
    echo "candidate harness build failed"; tail -20 /tmp/harness.log; report 0
fi
chmod 755 /tmp/h_base /tmp/h_cand

units_checked=0
speedups=()

echo "== correctness + measurement, per scored unit =="
while read -r unit; do
    [ -z "$unit" ] && continue
    units_checked=$((units_checked + 1))
    ok="exact"

    # The harness writes the digest stream to stdout when its output path is
    # "-", and this shell -- not the harness process -- owns the redirection
    # into DUMP_DIR.  The harness runs as the unprivileged identity, so a dump
    # written to a path of its own choosing in /tmp would be a file the
    # candidate's code could rewrite after the comparison's inputs existed.
    if ! run_unprivileged /tmp/h_base "$unit" dump - > "$DUMP_DIR/d_base.bin" 2>/dev/null; then
        echo "  $unit: base dump failed"; correct=0; continue
    fi
    if ! run_unprivileged /tmp/h_cand "$unit" dump - > "$DUMP_DIR/d_cand.bin" 2>/dev/null; then
        echo "  $unit: candidate dump failed"; correct=0; continue
    fi
    if ! cmp -s "$DUMP_DIR/d_base.bin" "$DUMP_DIR/d_cand.bin"; then
        ok="MISMATCH"; correct=0
    fi

    rounds=""
    tb=0; tc=0
    fp_checked=""
    r=1
    while [ "$r" -le "$NROUNDS" ]; do
        bout=$(run_unprivileged taskset -c 0,1 /tmp/h_base "$unit" bench "$NREPS_PER_ROUND" 2>/dev/null)
        cout=$(run_unprivileged taskset -c 0,1 /tmp/h_cand "$unit" bench "$NREPS_PER_ROUND" 2>/dev/null)
        # A bench run prints exactly one line: "<unit> <median> <samples...>
        # fp=<hex> fpn=<n>".  The candidate's own implementation runs inside
        # that process and shares its stdout, so a library that prints a line
        # of its own -- report-shaped or not -- is parsed in place of, or in
        # front of, the harness's number: `awk '{print $2}'` then reads the
        # first of two, and a line reading "1" prices the unit at the whole
        # base time.  So the stream must be exactly one line and that line must
        # be the harness's report; anything else is a failed round, not a
        # number.  An honest implementation writes nothing -- the harness owns
        # that channel.
        blines=$(printf '%s\n' "$bout" | awk 'END { print NR }')
        clines=$(printf '%s\n' "$cout" | awk 'END { print NR }')
        if [ "$blines" -ne 1 ] || [ "$clines" -ne 1 ] ||
            ! printf '%s' "$bout" | grep -qE "$REPORT_LINE" ||
            ! printf '%s' "$cout" | grep -qE "$REPORT_LINE"; then
            echo "  $unit: round $r: the harness report line is missing or not alone"
            echo "    base reported ${blines:-?} line(s), candidate ${clines:-?}"
            ok="NO-TIMING"; correct=0; break
        fi
        b=$(printf '%s\n' "$bout" | awk '{print $2}')
        c=$(printf '%s\n' "$cout" | awk '{print $2}')
        if [ -z "${b:-}" ] || [ -z "${c:-}" ] || [ "${b:-0}" -le 0 ] || [ "${c:-0}" -le 0 ]; then
            echo "  $unit: round $r produced no timing (base=${b:-none} cand=${c:-none})"
            ok="NO-TIMING"; correct=0; break
        fi
        # The timed roles fold every digest (and for the rolling hash the
        # returned offset and window state) they produced into the `fp=` field,
        # with `fpn=` counting the folded words.  Both builds drive the same
        # inputs, so the fold and the count must agree; a missing or doubled
        # field is a mismatch, not a skip.  This is what stops a tree whose
        # timed path is fast and wrong from being scored on medians nothing
        # ever checked.
        if [ -z "$fp_checked" ]; then
            bfp=$(printf '%s\n' "$bout" | awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^fp=/) print $i, $(i + 1) }')
            cfp=$(printf '%s\n' "$cout" | awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^fp=/) print $i, $(i + 1) }')
            if [ -z "$bfp" ] || [ "$bfp" != "$cfp" ]; then
                echo "  $unit: the timed path's outputs differ from the base build"
                echo "    base fp=${bfp:-none}"
                echo "    cand fp=${cfp:-none}"
                [ "$ok" = "exact" ] && ok="OUTPUT-MISMATCH"
                correct=0
            fi
            fp_checked=1
        fi
        tb=$b; tc=$c
        rounds="$rounds $(awk -v a="$b" -v c="$c" 'BEGIN { printf "%.4f", a / c }')"
        r=$((r + 1))
    done
    [ "$ok" = "NO-TIMING" ] && continue
    sp=$(awk -v s="$rounds" 'BEGIN {
        n = split(s, v, " ")
        for (i = 1; i <= n; i++) for (j = i + 1; j <= n; j++) if (v[j] < v[i]) { t = v[i]; v[i] = v[j]; v[j] = t }
        if (n % 2) printf "%.4f", v[(n + 1) / 2]
        else printf "%.4f", (v[n / 2] + v[n / 2 + 1]) / 2
    }')
    printf "  %-34s %12s -> %12s ns  %7sx  %s  (rounds:%s)\n" \
        "$unit" "$tb" "$tc" "$sp" "$ok" "$rounds"
    if awk -v s="$sp" 'BEGIN { exit !(s < 0.90) }'; then
        echo "  $unit: below 0.90x (regression gate disabled)"
    fi
    speedups+=("$sp")
done < "$FUNCS"

if [ "$units_checked" -eq 0 ]; then
    echo "no scored unit was examined"; report 0
fi
echo "  examined $units_checked scored unit(s)"

echo "== correctness: project's own suite, no new failures vs base =="
project_failures "$CAND" > /tmp/cand_fail.txt
# A test that no longer builds is not a test that passed: a failed prerequisite
# produces no failure marker, so a suite that will not build reports zero new
# failures.  The base's own count is recorded by vendor/build_base.sh at image
# build (one line); the candidate must run at least that many.
cand_ran=$(grep -c 'Completed run:' /tmp/checkrun.log || true)
if [ -s /tmp/cand_fail.txt ]; then
    comm -23 /tmp/cand_fail.txt "$BASE_FAILURES" > /tmp/new_fail.txt
else
    : > /tmp/new_fail.txt
fi
if [ -s /tmp/new_fail.txt ]; then
    echo "  NEW failures:"; sed 's/^/    /' /tmp/new_fail.txt; correct=0
else
    echo "  none (base fails $(wc -l < "$BASE_FAILURES") of its own tests, candidate $(wc -l < /tmp/cand_fail.txt))"
fi
base_ran=$(cat "$BASE_RAN" 2>/dev/null || true)
if [ -z "${base_ran:-}" ]; then
    echo "  no $(basename "$BASE_RAN") in this build context -- only the zero-test bound applies"
    base_ran=0
fi
if [ "${cand_ran:-0}" -eq 0 ]; then
    echo "  CANDIDATE RAN NO CHECK TESTS -- the suite or its runner is broken"; correct=0
elif [ "${cand_ran:-0}" -lt "${base_ran:-0}" ]; then
    echo "  candidate ran ${cand_ran} check test(s) against the base's ${base_ran}; a check that no longer builds is not a check that passed"; correct=0
else
    echo "  candidate suite ran ${cand_ran} check test(s)"
fi

echo "== C reference integrity: the C reference this milestone names vs the base =="
# The milestone names the C reference implementations off limits, and the
# candidate tree is the numerator of every ratio: a behaviour-preserving edit
# to the reference moves the graded figure while every other gate (digests,
# suite, envelope) still passes.  So each named file is compared against the
# pristine base, byte for byte.  A missing file fails like a modified one.
# REF.stepNN is per-step data, bound by digest like FUNCS/STAGE/harness.
if [ -r "$TESTS_DIR/REF.step$STEP" ]; then
    ref_n=0; ref_bad=0
    while read -r f; do
        [ -n "$f" ] || continue
        ref_n=$((ref_n + 1))
        if ! cmp -s "$BASE/$f" "$CAND/$f"; then
            echo "  $f: the C reference this milestone names was modified"; ref_bad=$((ref_bad + 1))
        fi
    done < "$TESTS_DIR/REF.step$STEP"
    if [ "$ref_bad" -gt 0 ]; then
        echo "  $ref_bad of $ref_n reference file(s) differ from the base build"; correct=0
    else
        echo "  all $ref_n reference file(s) match the base build byte for byte"
    fi
else
    echo "  no REF.step$STEP in this build context -- gate cannot run"; correct=0
fi

echo "== capability envelope: the candidate's own code vs what this host executes =="
# Scoped to this step: only the files this step owns are inspected, and only
# when the candidate changed them.  A tree that changed nothing in this step's
# scope is the tree it started from, so the check passes vacuously -- and says
# so, with the count it examined.
if [ -r "$TESTS_DIR/check_envelope.sh" ]; then
    source "$TESTS_DIR/check_envelope.sh"
    check_envelope "$BASE" "$CAND" "$STAGE_DIRS" "$STEP" || correct=0
else
    echo "  envelope: check_envelope.sh is missing from this build context -- gate cannot run"
    correct=0
fi

echo "== similarity diagnostic (non-scored) =="
# The candidate's added lines, trimmed and sorted, against upstream's line set
# for the same directories, baked into the image before the strip. Only the
# source directories the ladder owns are diffed: diffing the whole tree would
# also count the build system, which every step legitimately touches.
LADDER_DIRS="sha256_mb sha1_mb sha512_mb md5_mb sm3_mb mh_sha1 mh_sha1_murmur3_x64_128 mh_sha256 rolling_hash"
: > /tmp/added_lines.txt
for d in $LADDER_DIRS; do
    [ -d "$CAND/$d" ] || continue
    diff -ruN --unified=0 "$BASE/$d" "$CAND/$d" 2>/dev/null \
        | grep '^+' | grep -v '^+++' \
        | sed -E 's/^\+[ \t]*//; s/[ \t]+$//' >> /tmp/added_lines.raw
done
awk 'length($0) > 0' /tmp/added_lines.raw | sort -u > /tmp/added_lines.txt
rm -f /tmp/added_lines.raw
echo "  $(wc -l < /tmp/added_lines.txt) distinct added line(s)"

[ "${#speedups[@]}" -eq 0 ] && { echo "no unit measured"; report 0; }
report "$correct" "${speedups[@]}"
