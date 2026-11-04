#!/usr/bin/env bash

# --- verifier/step binding check -------------------------------------------
# /etc/harbor-step-id is derived in the image from the name of this step's
# data file; the file itself carries the same number. They disagree only when
# the build context came from another step's cache entry, in which case every
# check below would grade the wrong milestone. Reported, not crashed: a
# non-zero exit makes Harbor skip the remaining steps and buries the fault
# under a second symptom.
set -u
__hb_got=$(cat /etc/harbor-step-id 2>/dev/null) || __hb_got=MISSING
__hb_f=$(ls /tests/STEP.step* 2>/dev/null | head -n1)
__hb_want=""
[ -n "$__hb_f" ] && __hb_want=$(sed -n 's/^step=//p' "$__hb_f")
if [ -z "$__hb_want" ] || [ "$__hb_got" != "$__hb_want" ]; then
    __hb_msg="VERIFIER/STEP MISMATCH: image is step ${__hb_got}, /tests data file is '${__hb_f}' (step ${__hb_want:-none})."
    printf '%s\n' "$__hb_msg" >&2
    mkdir -p /logs/verifier 2>/dev/null
    printf '%s\n' "$__hb_msg" > /logs/verifier/step-mismatch.log
    printf '{"reward":0,"correctness":0,"verifier_error":"step_mismatch","image_step":"%s","tests_step":"%s"}\n' \
        "$__hb_got" "${__hb_want:-none}" > /logs/verifier/reward.json
    exit 0
fi

source /tests/helpers.sh

# --- reward directory hardening --------------------------------------------
# Harbor chmods /logs/verifier to 777 just before this container starts, so
# anything running in it -- including candidate code this verifier builds and
# runs below -- can overwrite reward.json. harden_reward_dir runs after that
# chmod, so root:root 0700 is the mode that sticks; on its own it protects
# nothing, because root can write a root-owned directory whatever its mode, so
# every piece of candidate code below runs through run_unprivileged (nobody)
# and every candidate-authored build gets its output directories handed to
# that user first.
harden_reward_dir

# --- per-step data ---------------------------------------------------------
# Read from a file whose *name* carries the step number, so BuildKit's
# (path,size,mtime) context dedup cannot serve a sibling's copy: distinct
# paths cannot collide even when the bytes are the same length.
STEP=$(sed -n 's/^step=//p' "$__hb_f")
LEVEL=$(sed -n 's/^level=//p' "$__hb_f")
FUNCS=$(sed -n 's/^funcs=//p' "$__hb_f")
IMAGES=$(sed -n 's/^images=//p' "$__hb_f")
# A step file that is present but carries no step number or no function list is
# a verifier fault, not a candidate failure. Report it as a zero score rather
# than exiting silently: a verifier that writes no reward is read by Harbor as
# a crash, and Harbor answers a crash by skipping the remaining steps.
[ -z "$STEP" ] && { echo "empty step file"; report 0; }
[ -z "$FUNCS" ] && { echo "no functions scoped"; report 0; }
unset __hb_got __hb_f __hb_want __hb_msg

# Runs in this step's verifier container on a fresh copy of /app.
#
#   correctness : 1 only if the candidate builds, the project's own suites gain
#                 no new failure against this step's base, the end-to-end
#                 key/ciphertext/shared-secret vectors are unchanged, every
#                 scoped function's check image is byte-identical to the base's,
#                 the candidate's own work inside this step's scope stays inside
#                 the ladder's capability envelope (AVX2 or narrower, one
#                 thread, the project's own build flags), and the end-to-end
#                 cost has not regressed against the candidate's own tree with
#                 this step's dispatch neutralised.
#   speedup     : one ratio per scoped function, from a harness that calls the
#                 entry point the library installed in both trees.
#   reward      : correctness * ln(geometric mean of those ratios)  [helpers.sh]
#
# Identical for every step. Per-step data: /tests/STEP.step<N>.

SRC=/app/mlkem-native
[ -d "$SRC" ] || { echo "missing $SRC"; report 0; }

echo "== step $STEP (base level $LEVEL): scope [$FUNCS] =="

# From here on the candidate's tree is the candidate's code: the Makefile that
# drives the build, the sources it compiles and the binaries it runs are all
# the candidate's own, so the tree is handed to the unprivileged user and every
# site that executes any of it runs through run_unprivileged for the rest of
# this script.
give_app_to_unprivileged

echo "== building candidate =="
build_tree "$SRC" /cand || report 0

correct=1

echo "== correctness: project's own suites, no new failures vs this step's base =="
suite_failures "$SRC" /cand > /tmp/cand_fail.txt
# Both sides must have been examined, or the comparison is vacuous.
nran=$(cat /tmp/suite_count.txt 2>/dev/null || echo 0)
if [ "$nran" -ne 6 ]; then echo "  only $nran of 6 suite targets ran"; correct=0; fi
newf=$(comm -23 /tmp/cand_fail.txt /base_suite_failures.txt)
nbase=$(wc -l < /base_suite_failures.txt)
ncand=$(wc -l < /tmp/cand_fail.txt)
echo "  targets run: $nran, base failures: $nbase, candidate failures: $ncand"
if [ -n "$newf" ]; then echo "  NEW failures:"; echo "$newf" | sed 's/^/    /'; correct=0; fi

# The base harnesses were built into the image; rebuild only if one is missing.
hb=0
for s in 512 768 1024; do
    cp -f "/base_harness_$s" "/tmp/hb_$s" 2>/dev/null \
        || bash /tests/vendor/harness.sh /base /basebld "$s" "/tmp/hb_$s" >/dev/null 2>&1
    [ -x "/tmp/hb_$s" ] && hb=$((hb+1))
done
[ "$hb" -eq 3 ] || { echo "  base harness missing for $((3-hb)) parameter set(s)"; correct=0; }

echo "== correctness: end-to-end KEM vectors vs base =="
nv=0
for s in 512 768 1024; do
    if run_unprivileged bash /tests/vendor/harness.sh "$SRC" /cand "$s" /tmp/hc_$s >/dev/null 2>&1 \
       && [ -x /tmp/hb_$s ]; then
        taskset -c 0,1 /tmp/hb_$s kem_vectors 9001 dump > /tmp/vb.bin 2>/dev/null
        run_unprivileged taskset -c 0,1 /tmp/hc_$s kem_vectors 9001 dump > /tmp/vc.bin 2>/dev/null
        if cmp -s /tmp/vb.bin /tmp/vc.bin; then echo "  ML-KEM-$s vectors identical"; else
            echo "  ML-KEM-$s vectors DIFFER"; correct=0; fi
        nv=$((nv+1))
    else
        echo "  ML-KEM-$s harness failed to build"; correct=0
    fi
done
[ "$nv" -eq 3 ] || { echo "  only $nv of 3 parameter sets compared"; correct=0; }

echo "== correctness: per-function check images vs base =="
nimg=0
for s in 512 768 1024; do
    for f in $IMAGES; do
        taskset -c 0,1 /tmp/hb_$s "$f" 4242 dump > /tmp/ib.bin 2>/dev/null
        run_unprivileged taskset -c 0,1 /tmp/hc_$s "$f" 4242 dump > /tmp/ic.bin 2>/dev/null
        if cmp -s /tmp/ib.bin /tmp/ic.bin; then :; else
            echo "  $f/$s IMAGE MISMATCH"; correct=0; fi
        nimg=$((nimg+1))
    done
done
echo "  $nimg images compared"
[ "$nimg" -gt 0 ] || { echo "  no image was compared"; correct=0; }

echo "== correctness: capability envelope (AVX2 / 256-bit / one thread / project flags) =="
# The envelope is stated as a fairness rule in every instruction; this is the
# check behind it.  The reference is at the ceiling --
# upstream ships no wider backend for any kernel in this ladder -- so a
# candidate at the ceiling is the reference's peer, and reaching past it would
# measure access to a machine the reference never had.
#
# Scoped to this step, never to the whole diff: this step's own files, its own
# dispatch seams, files the candidate added, and build files it changed.  A
# candidate that implemented an earlier step its own way is therefore not
# charged here for that earlier work.
#
# A tree that changed nothing in this step's scope is the tree it started
# from: there is no new code to be out of envelope.  The check then says so
# and prints the counts, so it cannot pass without having examined something.
envf=/tmp/envelope.txt
if python3 /tests/vendor/envelope.py "$SRC" /base "$STEP" > "$envf" 2>&1; then
    cat "$envf"
else
    cat "$envf"
    echo "  envelope check did not run to a verdict"
    correct=0
fi
if [ "$(grep -c '^ENVELOPE_VERDICT=' "$envf" 2>/dev/null || true)" != "1" ]; then
    echo "  envelope check produced no single verdict line; verifier fault"
    correct=0
fi
grep -q '^ENVELOPE_VERDICT=violation' "$envf" 2>/dev/null && correct=0

echo "== end-to-end non-regression gate =="
# Compared against the candidate's OWN tree with this step's dispatch
# neutralised, not against the task's reference ladder: the workspace is
# cumulative, so a base-relative comparison would re-charge a deficit
# inherited from an earlier step at every later one. Both sides carry the
# candidate's steps 1..k-1, so that cancels and the gate asks what it is for --
# did THIS step's changes slow the real pipeline?
rm -rf /tmp/cand_rev /tmp/cand_rev_bld
rev_ok=0
if cp -a "$SRC" /tmp/cand_rev 2>/dev/null; then
    rm -rf /tmp/cand_rev/build
    # strip.py is verifier-owned and runs as root; the tree it edits is a copy
    # of the candidate's, so the copy is handed to the unprivileged user before
    # the build and the harness compile consume it.
    if python3 /tests/vendor/strip.py /tmp/cand_rev --stub-only "$STEP" > /tmp/stub_rev.log 2>&1 \
       && { chown -R "$SCORE_UID:$SCORE_GID" /tmp/cand_rev 2>/dev/null || true; } \
       && build_tree /tmp/cand_rev /tmp/cand_rev_bld > /tmp/rev_build.log 2>&1 \
       && run_unprivileged bash /tests/vendor/harness.sh /tmp/cand_rev /tmp/cand_rev_bld 768 /tmp/hc_rev >/dev/null 2>&1; then
        rev_ok=1
    fi
fi
if [ "$rev_ok" = 1 ]; then
    read -r e_ratio e_cand_sum e_rev_sum <<EOF
$(e2e_regression /tmp/hc_768 /tmp/hc_rev 7)
EOF
    echo "  neutralised $e_rev_sum cycles, as delivered $e_cand_sum cycles (median ratio $e_ratio)"
    if awk -v r="$e_ratio" 'BEGIN{exit !(r != r || r > 1.25)}'; then
        echo "  REGRESSED by more than 25% end-to-end (regression gate disabled)"
    fi
else
    echo "  could not neutralise step $STEP in the candidate tree; gate skipped"
fi

echo "== per-function measurement =="
speedups=()
for s in 512 768 1024; do
    for f in $FUNCS; do
        read -r sp tb tc nrounds <<EOF
$(median_speedup "/tmp/hb_$s" "/tmp/hc_$s" "$f" "$(( 1000 + s ))" 9)
EOF
        # A missing number is never a speedup: it is a broken tree on the
        # candidate's side and a verifier fault on the base's, and both are
        # reported rather than folded into the ratio.
        if [ "$sp" = "none" ]; then
            echo "  $f/$s: no measurement produced"; correct=0; sp=0
        elif [ "$tb" = "?" ] || [ "$tc" = "?" ]; then
            echo "  $f/$s: one side produced no measurement"; correct=0; sp=0
        fi
        printf "  %-18s %5s  %10s -> %10s   %sx  (%s rounds)\n" \
            "$f" "$s" "$tb" "$tc" "$sp" "$nrounds"
        speedups+=("$sp")
    done
done

[ "${#speedups[@]}" -eq 0 ] && { echo "no functions measured"; report 0; }
report "$correct" "${speedups[@]}"
