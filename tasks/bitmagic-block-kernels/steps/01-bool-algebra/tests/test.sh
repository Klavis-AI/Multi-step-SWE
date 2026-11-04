#!/usr/bin/env bash

# --- test-image/step binding check -----------------------------------------
# /etc/harbor-step-id names the IMAGE, written by this step's Dockerfile as a
# literal.  /tests/STEPID.stepNN names the CONTEXT this tests/ tree came from,
# and its file name carries the step.  They disagree when the build context was
# served from another step's cache entry, in which case every check below would
# run against the wrong milestone.  Reported, not crashed: a non-zero exit
# makes the runtime skip the remaining steps and buries the fault under a
# second symptom.
__hb_want=$(cat /tests/STEPID.step* 2>/dev/null | tr -d '[:space:]')
__hb_got=$(cat /etc/harbor-step-id 2>/dev/null) || __hb_got=MISSING
if [ -z "$__hb_want" ] || [ "$__hb_got" != "$__hb_want" ]; then
    __hb_msg="VERIFIER/STEP MISMATCH: image is step '${__hb_got}', /tests tree is step '${__hb_want}'."
    printf '%s\n' "$__hb_msg" >&2
    mkdir -p /logs/verifier 2>/dev/null
    printf '%s\n' "$__hb_msg" > /logs/verifier/step-mismatch.log
    # Numbers only.  Every key under `rewards` is parsed as a float, so a string
    # field here makes the output fail to parse and the step is dropped from the
    # trial denominator -- a harness symptom that looks like a task failure.
    # The message goes to stderr and to step-mismatch.log instead.
    printf '{"reward":0,"correctness":0,"geomean_speedup":1.0}\n' \
        > /logs/verifier/reward.json
    exit 0
fi
unset __hb_want __hb_got __hb_msg
# --- end binding check ------------------------------------------------------
set -u
source /tests/helpers.sh

# --- reward hardening, before any candidate code runs ----------------------
# The session runtime chmods /logs/verifier to 777 as root just before this
# container starts, so anything running here can overwrite reward.json --
# including candidate code the checks build or run out of the candidate's
# tree.  Writing the real reward last does not help: a detached child outlives
# the reporting process and writes after it.  Take the directory back to
# root:root 0700 first (this runs after that chmod, so this mode sticks); it is
# not enough on its own, because root can write a root-owned directory whatever
# its mode, so every piece of candidate code below goes through run_unprivileged
# as well.
# `report` reopens the directory to 0755 on the way out -- still root-owned,
# so the candidate still cannot write it.
harden_reward_dir

# setpriv is what run_unprivileged drops privileges with, and it comes from
# util-linux, installed by this step's tests/Dockerfile.  If it is missing the
# candidate would run as root and the reward would be forgeable again: report
# that loudly instead of reporting a pass.
if ! command -v setpriv > /dev/null 2>&1; then
    echo "VERIFIER MISCONFIGURATION: setpriv (util-linux) is missing -- candidate code cannot be run unprivileged" >&2
    mkdir -p /logs/verifier 2>/dev/null || true
    printf '%s\n' "setpriv (util-linux) is missing: candidate code cannot be run unprivileged" \
        > /logs/verifier/verifier-misconfigured.log
    # Numbers only: every key under `rewards` is parsed as a float.
    printf '{"reward":0,"correctness":0,"geomean_speedup":1.0}\n' > /logs/verifier/reward.json
    exit 0
fi

# /app arrives owned by whoever wrote the artifact, and every build below runs
# as nobody: without this handover the build cannot create its output and every
# candidate would score 0.  /base and /tests are the verifier's own trees and
# are never handed over.  The verifier's own probes (/base_harness, /base_e2e)
# stay root-owned and run as root; only binaries built from the candidate's
# tree run unprivileged, through run_side.
give_app_to_unprivileged

# Runs in this step's verifier container on a fresh copy of /app.
#
#   correctness : 1 only if the candidate builds, every in-scope entry point
#                 reproduces this step's base byte for byte on randomised
#                 inputs -- and the timed sequence's own outputs match it too --
#                 the end-to-end digest is unchanged, the project's own
#                 build system still configures, the composite pipeline has not
#                 regressed, and -- for a tree that changed any file, this
#                 milestone's files included -- the added code is inside the
#                 AVX2 envelope.  A tree that changed nothing anywhere passes
#                 the envelope gate vacuously and says so: correctness asks
#                 whether behaviour changed, and it did not.
#   speedup     : one ratio per in-scope entry point, from a harness that calls
#                 the implementation the LIBRARY installs for that entry, in both
#                 trees, on the same inputs.
#   reward      : 1 when correctness is 1 and the geometric mean is above the
#                 step's baseline, otherwise 0                      [helpers.sh]
#
# Identical for every step.  Per-step data: tests/FUNCS.stepNN,
# tests/STEPID.stepNN and tests/bench_kernels.stepNN.cpp.

# Run one measurement binary, pinned to the same two cores either way.
#
#   /base_harness, /base_e2e -- built inside this verifier image from /base,
#       the verifier's own tree.  Verifier-owned code, run as root.
#   anything else (i.e. /tmp/h_cand, /tmp/e2e_cand) -- built here from the
#       CANDIDATE's headers, so running it runs candidate code, static
#       initializers included.  It runs as nobody, so it cannot write
#       /logs/verifier whatever this script's own mode left behind.
#
# Keeping the choice in one place is what makes "no candidate binary runs as
# root" checkable: every candidate binary below appears as an argument of
# run_side and nowhere else.
run_side() {
    case "${1:-}" in
        /base|/base_*) taskset -c 0,1 "$@" ;;
        *)             run_unprivileged taskset -c 0,1 "$@" ;;
    esac
}

SRC=/app/bitmagic
[ -d "$SRC" ] || { echo "missing $SRC"; report 0; }

# Scratch for the comparison files (base dump, candidate dump, the two trace
# files).  A root-only directory, deliberately: the candidate's binary is what
# writes the candidate side, and if these paths lived in /tmp -- which is
# world-writable -- a candidate process could pre-create one of them as a
# symlink before the verifier's first redirection.  Root's own redirection
# would then follow it, and `cmp` would end up comparing one file with itself,
# silently passing the per-entry gate for any kernel.  Root owns this
# directory, so nothing the candidate runs can create, replace or link
# anything inside it.  Nothing about what is compared changes: the same two
# streams are written and compared byte for byte.  The template in /tmp is
# explicit so a stray TMPDIR in the verifier's environment cannot move the
# scratch somewhere unwritable and take every candidate down with it.
CMPDIR=$(mktemp -d /tmp/hb-cmp.XXXXXXXX) || { echo "could not create a private comparison directory"; report 0; }
chmod 700 "$CMPDIR" 2>/dev/null || true

HFILE=$(ls -1 /tests/bench_kernels.step*.cpp 2>/dev/null)
FUNCS_F=$(ls -1 /tests/FUNCS.step* 2>/dev/null)
if [ -z "$HFILE" ] || [ -z "$FUNCS_F" ]; then
    echo "per-step data files missing under /tests"; report 0
fi

# Verifier-owned input draws.  The candidate's shipped harness uses a different
# published seed, so the practice set is not the measured set.
VSEED_CORRECT_A=771303017
VSEED_CORRECT_B=405030941
VSEED_BENCH=559038737
ESEED=314159265

correct=1

echo "== building candidate =="
build_e2e "$SRC" /tests/e2e_bench.cpp /tmp/e2e_cand || report 0
build_harness "$SRC" "$HFILE" /tmp/h_cand || report 0

echo "== correctness: end-to-end digest vs this step's base (L0) =="
d_base=$(run_side /base_e2e digest "$ESEED" 2>/dev/null)
d_cand=$(run_side /tmp/e2e_cand digest "$ESEED" 2>/dev/null)
if [ -n "$d_base" ] && [ "$d_base" = "$d_cand" ]; then
    echo "  identical: $d_base"
else
    echo "  base=$d_base candidate=$d_cand -- MISMATCH"; correct=0
fi

echo "== envelope gate: capability envelope, scoped to this milestone's files =="
envelope_check "$SRC" || correct=0

echo "== correctness: per-function, byte-identical vs the base build =="
nchecked=0
ntraced=0
while read -r fnid; do
    [ -z "$fnid" ] && continue
    for s in "$VSEED_CORRECT_A" "$VSEED_CORRECT_B"; do
        for v in 0 1 2; do
            if ! run_side /base_harness "$fnid" "$s" "$v" dump > "$CMPDIR"/d_base.bin 2>/dev/null; then
                echo "  $fnid: base dump failed"; correct=0; continue
            fi
            if ! run_side /tmp/h_cand "$fnid" "$s" "$v" dump > "$CMPDIR"/d_cand.bin 2>/dev/null; then
                echo "  $fnid: candidate dump failed (seed $s variant $v)"; correct=0; continue
            fi
            nchecked=$((nchecked + 1))
            if ! cmp -s "$CMPDIR"/d_base.bin "$CMPDIR"/d_cand.bin; then
                echo "  $fnid: MISMATCH (seed $s variant $v)"; correct=0
            fi
        done
    done
    # The timed path itself: `trace` replays the call sequence `bench` times on
    # the seed the measurement uses -- the same draws, in the same order -- and
    # prints every output of every call, so a body that answers from a cache or
    # skips the work where the timing happens is compared against the base on the
    # calls that are timed, not only in the single call `dump` prints.
    for v in 0 1 2; do
        if ! run_side /base_harness "$fnid" "$VSEED_BENCH" "$v" trace 5 > "$CMPDIR"/t_base.bin 2>/dev/null; then
            echo "  $fnid: base trace failed"; correct=0; continue
        fi
        if ! run_side /tmp/h_cand "$fnid" "$VSEED_BENCH" "$v" trace 5 > "$CMPDIR"/t_cand.bin 2>/dev/null; then
            echo "  $fnid: candidate trace failed (variant $v)"; correct=0; continue
        fi
        if [ ! -s "$CMPDIR"/t_base.bin ]; then
            echo "  $fnid: base trace was empty (variant $v) -- nothing was compared"; correct=0; continue
        fi
        ntraced=$((ntraced + 1))
        if ! cmp -s "$CMPDIR"/t_base.bin "$CMPDIR"/t_cand.bin; then
            echo "  $fnid: TRACE MISMATCH (variant $v) -- the timed sequence's outputs differ from the base's"; correct=0
        fi
    done
done < "$FUNCS_F"
echo "  $nchecked comparison(s) run over $(grep -c . "$FUNCS_F") entry point(s)"
if [ "$nchecked" -eq 0 ]; then echo "  no comparison was run"; correct=0; fi
echo "  $ntraced trace comparison(s) of the timed sequence run"
if [ "$ntraced" -eq 0 ]; then echo "  no trace comparison was run"; correct=0; fi

echo "== correctness: the project's own build system still configures =="
configure_project "$SRC" /tmp/candb || correct=0

echo "== end-to-end non-regression gate (baseline = the ladder's L0 floor) =="
# The base/candidate ratio of two IDENTICAL trees varies widely, and whichever
# side runs first is systematically slower -- so a one-sided ordering
# manufactures a regression.
# Three defences: a warm-up call per side before anything is timed, alternating
# the order inside each pair, and a threshold set at a multiple rather than a
# few percent.  What the gate is for is a pipeline that has genuinely regressed,
# which is a 2x-ish effect; a few percent here is not measurable.
run_side /base_e2e bench "$ESEED" > /dev/null 2>&1
run_side /tmp/e2e_cand bench "$ESEED" > /dev/null 2>&1
ratios=(); order=""
for pair in 1 2 3 4; do
    if [ $((pair % 2)) -eq 1 ]; then first=/base_e2e; second=/tmp/e2e_cand; else first=/tmp/e2e_cand; second=/base_e2e; fi
    a=$(run_side "$first"  bench "$ESEED" 2>/dev/null)
    b=$(run_side "$second" bench "$ESEED" 2>/dev/null)
    if [ "$first" = "/base_e2e" ]; then r=$(awk -v x="$a" -v y="$b" 'BEGIN{ if (y+0<=0) print 0; else printf "%.4f", x/y }')
    else                     r=$(awk -v x="$b" -v y="$a" 'BEGIN{ if (y+0<=0) print 0; else printf "%.4f", x/y }'); fi
    [ -n "$a" ] && [ -n "$b" ] && { ratios+=("$r"); order="$order $r"; }
done
if [ "${#ratios[@]}" -ge 4 ]; then
    er=$(printf '%s\n' "${ratios[@]}" | sort -g | awk 'NR==2||NR==3{s+=$1}END{printf "%.4f", s/2}')
    echo "  pair ratios (L0 floor / candidate):$order   median-of-4 $er"
    if awk -v r="$er" 'BEGIN{exit !(r < 1/1.60)}'; then
        echo "  REGRESSED: the pipeline is more than 60% slower than the L0 floor (regression gate disabled)"
    fi
else
    echo "  could not time both sides (${#ratios[@]} complete pair(s))"; correct=0
fi

echo "== per-function measurement =="
speedups=()
while read -r fnid; do
    [ -z "$fnid" ] && continue
    for v in 0 1 2; do
        ratios=()
        for pass in 1 2 3; do
            # alternate the two sides inside each pass so a drift in host load
            # cancels instead of landing on one of them
            tb=$(run_side /base_harness "$fnid" "$VSEED_BENCH" "$v" bench 5 2>/dev/null)
            tc=$(run_side /tmp/h_cand "$fnid" "$VSEED_BENCH" "$v" bench 5 2>/dev/null)
            if [ -z "$tb" ] || [ -z "$tc" ]; then echo "  $fnid/v$v: no timing"; ratios=(); break; fi
            ratios+=("$(awk -v a="$tb" -v b="$tc" 'BEGIN{ if (b+0<=0) print 0; else printf "%.4f", a/b }')")
        done
        if [ "${#ratios[@]}" -eq 0 ]; then correct=0; continue; fi
        # median of three passes, then the harness's own value is already a
        # median of five batches; best-of-N is deliberately not used (workload
        # doc section 8: it preserves spuriously fast runs, which a ratio
        # reports as a speedup)
        sp=$(printf '%s\n' "${ratios[@]}" | sort -g | sed -n '2p')
        speedups+=("$sp")
        printf '  %-24s v%s  base %10s ns  cand %10s ns  %6sx\n' "$fnid" "$v" "${tb:-?}" "${tc:-?}" "$sp"
    done
done < "$FUNCS_F"

[ "${#speedups[@]}" -eq 0 ] && { echo "no functions measured"; report 0; }
report "$correct" "${speedups[@]}"
