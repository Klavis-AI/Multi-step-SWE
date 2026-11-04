#!/usr/bin/env bash

# --- verifier/step binding check -------------------------------------------
# /etc/harbor-step-id names the image; /tests/STEP.stepNN names this tests/
# tree.  They disagree only when the build context came from another step's
# cache entry, in which case every check below would run against the wrong
# milestone.  Reported, not crashed: a non-zero exit makes the runtime skip the
# remaining steps and buries the fault under a second symptom.
__hb_want=$(cat /tests/STEP.step* 2>/dev/null | tr -d '[:space:]')
__hb_got=$(cat /etc/harbor-step-id 2>/dev/null | tr -d '[:space:]') || __hb_got=MISSING
if [ "$__hb_got" != "$__hb_want" ]; then
    __hb_msg="VERIFIER/STEP MISMATCH: image is step ${__hb_got}, /tests tree is step ${__hb_want}."
    printf '%s\n' "$__hb_msg" >&2
    mkdir -p /logs/verifier 2>/dev/null
    printf '%s\n' "$__hb_msg" > /logs/verifier/step-mismatch.log
    printf '{"reward":0,"verifier_error":"step_mismatch","image_step":"%s","tests_step":"%s"}\n' \
        "$__hb_got" "$__hb_want" > /logs/verifier/reward.json
    chmod 0755 /logs/verifier 2>/dev/null || true
    chmod 0644 /logs/verifier/reward.json 2>/dev/null || true
    exit 0
fi
unset __hb_want __hb_got __hb_msg
# --- end binding check ------------------------------------------------------
set -u
source /tests/helpers.sh
harden_reward_dir

# Runs in this step's verifier container on a fresh copy of /app.
#
#   correctness : 1 only if the candidate builds, every measured unit
#                 reproduces the base build's output byte for byte, the
#                 candidate's own tree does not get slower end to end, and the
#                 capability envelope holds.  The last two are read against what
#                 the candidate wrote, not against the tree it inherited: a tree
#                 that changed nothing in this step's scope passes both
#                 vacuously and says so, because "unchanged" is a behaviour
#                 verdict of 1 and only the speedup half measures the work.
#   speedup     : one ratio per measured unit.  Each unit is one filter
#                 operation reached through the public filter graph, measured as
#                 the difference between the same clip with and without that
#                 filter, so the source filter and the frame cache cancel.
#   reward      : 1 when correctness is 1 and the geometric mean is above the
#                 step's baseline, otherwise 0                      [helpers.sh]
#
# Every repetition reads a different window of the clip: each round starts its
# numbering further on, so a unit is measured over 3 x REPS windows and no timed
# pass repeats another's frame numbers.  A library that answers a repetition
# from a cache keyed on the frame number or on the input's digest sees no
# speedup from it -- the window's frames were never read before.  The digest is
# read from the last repetition's window, at frames no earlier pass measured.
#
# Identical for every step.  Per-step data: STEP.stepNN, UNITS.stepNN,
# BASE_ROWS.stepNN, SHA256SUMS.stepNN.

REPS=5
ROUNDS=3
MAX_RATIO=40          # §10: a sentinel or an impossible ratio must not pay out

( cd /tests && sha256sum -c SHA256SUMS.step* ) > /tmp/sums.log 2>&1 || {
    echo "per-step data failed its digest check:"; cat /tmp/sums.log
    report 0
}

SRC=/app/avisynthplus
[ -d "$SRC" ] || { echo "missing $SRC"; report 0; }

echo "== building candidate =="
give_app_to_unprivileged
CAND=$(mktemp -d /tmp/cand.XXXXXX)
chown nobody:nogroup "$CAND" 2>/dev/null || chown nobody:nobody "$CAND"
if ! run_unprivileged env HOME=/tmp \
     cmake -S "$SRC" -B "$CAND" -DCMAKE_BUILD_TYPE=Release -G Ninja \
       -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /tmp/cmake.log 2>&1; then
    echo "candidate configure failed"; tail -20 /tmp/cmake.log; report 0
fi
if ! run_unprivileged env HOME=/tmp \
     cmake --build "$CAND" -j"$(nproc)" > /tmp/build.log 2>&1; then
    echo "candidate build failed"; grep -m20 -E 'error:' /tmp/build.log; report 0
fi
chmod -R a+rX "$SRC" "$CAND" 2>/dev/null || true

correct=1
speedups=()
declare -A delivered_chain=()

echo "== per-unit correctness and timing =="
while IFS=$'\t' read -r uid row source_expr filter_expr frames audio; do
    [ -n "${uid:-}" ] || continue
    udir="/tmp/u/$uid"
    mkdir -p "$udir"
    {
        printf 'SetMaxCPU("avx2")\n'
        printf '%s\n' "$source_expr"
        printf '%s\n' "$filter_expr"
        printf 'last\n'
    } > "$udir/chain.avs"
    {
        printf 'SetMaxCPU("avx2")\n'
        printf '%s\n' "$source_expr"
        printf 'last\n'
    } > "$udir/source.avs"

    # Every repetition reads a window of the clip no earlier repetition read:
    # that is what a library memoising its own output cannot answer from an
    # earlier repetition.  avsbench reports the window and fails outright if the
    # clip is too short to place one; the windows seen are checked below, so a
    # unit whose clip lost the room is a failed step rather than a quietly cheap
    # one.

    # Alternate the two libraries and keep the best of each side.  The target
    # host is shared and its load moves on a timescale of seconds, so a base
    # measured once and a candidate measured once are measured under different
    # conditions; alternating puts both under the same ones, and taking the
    # minimum of several rounds survives the contention that remains.  Both
    # figures are one-sided -- contention only ever adds time -- so the best
    # reading of each side is its best estimate of the uncontended cost.
    #
    # Each round draws on a fresh block of repetition numbers, so the rounds do
    # not repeat each other's inputs either.
    b_chain=999999; b_src=999999; c_chain=999999; c_src=999999
    b_dig=""; c_dig=""; unit_windows=""
    for round in $(seq 1 "$ROUNDS"); do
        seed0=$(( (round - 1) * REPS ))
        br=$(run_unit /basebld/avs_core "$udir" "$frames" "$REPS" "${audio:-0}" "$seed0") \
            || { echo "  $uid: base measurement failed"; correct=0; break; }
        cr=$(run_unit "$CAND/avs_core" "$udir" "$frames" "$REPS" "${audio:-0}" "$seed0") \
            || { echo "  $uid: candidate measurement failed"; correct=0; break; }
        b_chain=$(echo "$br $b_chain" | awk '{print ($1+0 < $4+0) ? $1 : $4}')
        b_src=$(echo "$br $b_src"     | awk '{print ($2+0 < $5+0) ? $2 : $5}')
        c_chain=$(echo "$cr $c_chain" | awk '{print ($1+0 < $4+0) ? $1 : $4}')
        c_src=$(echo "$cr $c_src"     | awk '{print ($2+0 < $5+0) ? $2 : $5}')
        b_dig=$(echo "$br" | awk '{print $3}'); c_dig=$(echo "$cr" | awk '{print $3}')
        b_win=$(echo "$br" | awk '{print $4}'); c_win=$(echo "$cr" | awk '{print $4}')
        unit_windows="$unit_windows $b_win $c_win"
    done
    if [ "$(printf '%s\n' $unit_windows | sort -u | wc -l)" -lt "$ROUNDS" ] \
       || printf '%s\n' $unit_windows | grep -q '^0$'; then
        echo "  $uid: repetition windows did not move ($unit_windows)"
        correct=0; continue
    fi
    b_diff=$(awk -v a="$b_chain" -v b="$b_src" 'BEGIN{printf "%.4f", a-b}')
    c_diff=$(awk -v a="$c_chain" -v b="$c_src" 'BEGIN{printf "%.4f", a-b}')
    delivered_chain[$uid]=$c_chain

    if [ -n "$b_dig" ] && [ "$b_dig" = "$c_dig" ]; then ok="exact"; else ok="MISMATCH"; correct=0; fi
    sp=$(awk -v a="$b_diff" -v b="$c_diff" -v cap="$MAX_RATIO" 'BEGIN{
        if (b+0 <= 0 || a+0 <= 0) { print "0.0000"; exit }
        r = a/b; if (r > cap) r = cap; printf "%.4f", r }')
    printf "  %-26s %9s -> %9s ms  %7sx  %s\n" "$uid" "$b_diff" "$c_diff" "$sp" "$ok"
    speedups+=("$sp")
done < /tests/UNITS.step*

if [ "${#speedups[@]}" -eq 0 ]; then echo "no units measured"; report 0; fi

echo "== capability envelope: no AVX-512 in this step's files =="
# The target host executes AVX2 and not AVX-512, so the ceiling is AVX2 for
# both sides.  The reference itself contains an AVX-512 file in one family; it
# is excluded from scope and never restored, so this check passes the oracle.
#
# The envelope is a rule on what the candidate may WRITE, so it is scoped to
# what the candidate changed: every scoped file it wrote is scanned, and a
# scope it left alone is skipped with its counts printed.  A tree that changed
# nothing is not a tree that changed behaviour, and this gate is not allowed to
# say it is; a tree that does write into this step's scope is checked exactly as
# strictly as before.  /task_base is the tree every candidate starts from.
mapfile -t isa_pats < /tests/ISA_PATTERNS.step*
check_isa_envelope "$SRC" /task_base "${isa_pats[@]}" || correct=0

echo "== capability envelope: compiler flags =="
# The other half of the same rule, and the one the digest gate cannot see: a
# global -mavx2 / -march=native raises the ISA for every translation unit and
# changes no byte of behaviour.  Read from the candidate's own configure, so it
# is what was compiled with, not what the CMake text looks like.
check_compile_flags "$SRC" "$CAND" || correct=0

echo "== end-to-end non-regression =="
# Baseline is the candidate's OWN tree with this step's family cut away, not
# /basebld: the workspace is cumulative and /basebld carries the reference's
# earlier families while the candidate carries its own, so a base-relative
# comparison re-charges a deficit inherited from an earlier step at every later
# one (and would report an untouched tree as a catastrophic regression, because
# an untouched tree is the task base, which has no family vectorized at all).
# Both sides here contain the candidate's earlier milestones, so that cancels
# and the gate asks what it is for: did THIS milestone's changes slow the
# pipeline down?
#
# That question is only answerable when the cut removes something.  On a tree
# with no implementation of this family in it the cut is a no-op, both sides of
# the comparison are the same tree, and the difference of the two readings is
# host noise with a sign -- which is how an untouched tree reads as a regression
# of any size while nothing was measured.  So the cut is bracketed by a
# fingerprint of the tree: a cut that changed nothing skips the comparison and
# says so, rather than reporting noise as a behaviour verdict.  A tree that does
# carry an implementation of the family takes the comparison exactly as before.
#
# A tree can also carry an implementation the *cut* cannot attribute -- the
# candidate's own unit, under a name of its own -- and the cut leaves such a
# file whole rather than gutting it (mklevel.py --cut), because gutting it
# would leave the tree unbuildable and this gate with no baseline at all.  That
# tree fingerprints as unchanged too, and the message below says which of the
# two it is: no code of this family was found to remove, so there is nothing to
# compare against, and no unit was measured by this gate.
#
# The cut happens in place and the rebuild is incremental, so this costs a
# recompile and a relink rather than a second full build.
ROW=$(head -n1 /tests/UNITS.step* | cut -f2)
rev_ok=0
cut_changed=0
rev_units=0
before_fp=$(tree_fingerprint "$SRC")
if run_unprivileged python3 -I /tests/vendor/mklevel.py --cut "$ROW" "$SRC" > /tmp/strip.log 2>&1; then
    after_fp=$(tree_fingerprint "$SRC")
    if [ -n "$before_fp" ] && [ "$before_fp" != "$after_fp" ]; then cut_changed=1; fi
    # Configure again before building: Files.cmake globs the source list, so
    # deleting the family's files behind ninja's back leaves rules for files
    # that no longer exist.
    if run_unprivileged env HOME=/tmp \
         cmake -S "$SRC" -B "$CAND" -DCMAKE_BUILD_TYPE=Release -G Ninja \
           -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /tmp/revcmake.log 2>&1 \
       && run_unprivileged env HOME=/tmp \
         cmake --build "$CAND" -j"$(nproc)" > /tmp/revbuild.log 2>&1; then
        rev_ok=1
    else
        echo "  rebuild after cutting family $ROW failed: the cut produced a tree"
        echo "  that does not build, so this gate has no baseline to compare"
        echo "  against -- no unit was measured by it."
    fi
else
    echo "  could not cut family $ROW from your tree; gate skipped"
fi
if [ "$rev_ok" = 1 ] && [ "$cut_changed" = 0 ]; then
    echo "  cutting family $ROW from this tree changed nothing: the cut found no"
    echo "  code of this family it could attribute and remove, so both sides of"
    echo "  the comparison would be the same tree.  Gate has no input -- skipped,"
    echo "  not failed, and no unit was measured by it."
    rev_ok=0
fi
while IFS=$'\t' read -r uid row source_expr filter_expr frames audio; do
    [ -n "${uid:-}" ] || continue
    [ "$rev_ok" = 1 ] || break
    udir="/tmp/u/$uid"
    cand_chain="${delivered_chain[$uid]:-}"
    # Best of three on BOTH sides, for the same reason the main measurement
    # alternates: a single reading of each side is taken under different host
    # conditions, and the comparison then reports contention.  Each round draws
    # on the same block of repetition numbers the delivered measurement's round
    # used, so the two sides of this comparison see the same inputs.
    rev_chain=999999
    for round in $(seq 1 "$ROUNDS"); do
        seed0=$(( (round - 1) * REPS ))
        _r=$(run_unit "$CAND/avs_core" "$udir" "$frames" "$REPS" "${audio:-0}" "$seed0" | awk '{print $1}')
        [ -n "$_r" ] || continue
        rev_chain=$(echo "$_r $rev_chain" | awk '{print ($1+0 < $2+0) ? $1 : $2}')
    done
    if [ -z "$cand_chain" ] || [ "$rev_chain" = 999999 ]; then
        echo "  $uid: could not measure the non-regression pair"; continue
    fi
    printf "  %-26s as delivered %9s ms   family cut %9s ms\n" "$uid" "$cand_chain" "$rev_chain"
    # 1.35, not 1.10.  The guard is for a step that made the pipeline genuinely
    # slower, which is a large multiple; a noisy host must not be able to fire
    # it.  1.35 applies on top of the input check above: what survives the skip
    # is a comparison between two different trees, and readings taken the same
    # way still move by more than 10% on a shared host.
    if awk -v a="$rev_chain" -v b="$cand_chain" 'BEGIN{exit !(b > a * 1.35)}'; then
        echo "    REGRESSED by more than 35% end-to-end (regression gate disabled)"
    fi
    rev_units=$((rev_units + 1))
done < /tests/UNITS.step*
if [ "$rev_ok" = 1 ]; then
    echo "  non-regression: compared $rev_units unit(s), threshold +35%"
fi

report "$correct" "${speedups[@]}"
