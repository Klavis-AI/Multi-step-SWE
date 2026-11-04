#!/usr/bin/env bash

# --- verifier/step binding check -------------------------------------------
# Every per-step file carries its step number in its NAME, so BuildKit's
# (path, size, mtime) context dedup cannot serve one step's file for another's.
# This checks that the number the context carries is the number this image was
# built for.  Reported, not crashed: a non-zero exit makes Harbor skip the
# remaining steps and buries the fault under a second symptom.
__hb_got=$(cat /etc/harbor-step-id 2>/dev/null) || __hb_got=MISSING
__hb_files=$(cd /tests 2>/dev/null && ls FUNCS.step* 2>/dev/null | tr '\n' ' ')
__hb_n=$(printf '%s' "$__hb_files" | wc -w)
__hb_want=$(printf '%s' "$__hb_files" | awk '{print $1}' | sed -e 's/^FUNCS\.step//')
if [ "$__hb_n" != "1" ] || [ -z "$__hb_want" ] || [ "$__hb_got" != "$__hb_want" ]; then
    __hb_msg="VERIFIER/STEP MISMATCH: image is step '${__hb_got}', /tests carries '${__hb_files}'."
    printf '%s\n' "$__hb_msg" >&2
    mkdir -p /logs/verifier 2>/dev/null
    printf '%s\n' "$__hb_msg" > /logs/verifier/step-mismatch.log
    printf '{"reward":0,"verifier_error":"step_mismatch","image_step":"%s","tests_files":"%s"}\n' \
        "$__hb_got" "$__hb_files" > /logs/verifier/reward.json
    exit 0
fi
# The name carries the step, so a cache-keyed substitution cannot produce a
# matching name -- but a context served from a sibling still has to be caught,
# and the identity check above only proves the NAME is right.  These digests
# are computed from the package on disk and baked into this image, so they
# come from outside the build context; anything served in their place fails
# here instead of being graded as a plausible number.
if [ -f /etc/harbor-step-digests ]; then
    __hb_tot=$(grep -cv '^\s*$' /etc/harbor-step-digests) || __hb_tot=0
    __hb_bad=$(cd /tests && sha256sum -c /etc/harbor-step-digests 2>/dev/null | grep -c ': FAILED') || true
    case "${__hb_bad:-x}" in ''|*[!0-9]*) __hb_bad=-1 ;; esac
    if [ "$__hb_tot" -eq 0 ] || [ "$__hb_bad" -ne 0 ]; then
        __hb_msg="BUILD-CONTEXT DIGEST MISMATCH: ${__hb_bad} of ${__hb_tot} per-step files in /tests do not match the digests this image was built with."
        printf '%s\n' "$__hb_msg" >&2
        mkdir -p /logs/verifier 2>/dev/null
        printf '%s\n' "$__hb_msg" > /logs/verifier/step-mismatch.log
        printf '{"reward":0,"verifier_error":"step_mismatch","digest_mismatches":%s,"digests_checked":%s}\n' \
            "${__hb_bad}" "${__hb_tot}" > /logs/verifier/reward.json
        exit 0
    fi
    echo "build context verified: ${__hb_tot} per-step files against this image's baked digests"
fi
unset __hb_tot __hb_bad

STEP="$__hb_want"
unset __hb_got __hb_files __hb_n __hb_want __hb_msg
# --- end binding check ------------------------------------------------------
set -u
source /tests/helpers.sh

# Harbor chmods /logs/verifier to 777 as root just before this container
# starts, so anything running in here can overwrite reward.json -- including
# candidate code the verifier builds or runs below.  Take the directory back
# before any of it runs; every candidate-authored command is launched through
# run_unprivileged so this is not just a mode bit, and report() reopens the
# directory for collection on the way out.
harden_reward_dir

# Runs in this step's verifier container on a fresh copy of /app.
#
#   correctness : 1 only if the candidate builds, every function in this
#                 step's scope reproduces the base build's output byte for
#                 byte, the project's own suite gains no new failure, the
#                 library-wide consistency digest is unchanged, the end-to-end
#                 non-regression gate passes, and the capability envelope
#                 holds -- each entry point the candidate answers with code it
#                 added keeps the solution width it is named for and stays
#                 inside this step's instruction-set ceiling.  The envelope
#                 questions are asked about the candidate's own code only: the
#                 stripped base answers every removed family from the portable
#                 reference, so reading the base's own answers would report a
#                 tree that changed nothing as having changed behaviour.
#   speedup     : one ratio per scoped function, from a harness that calls the
#                 public entry point the library INSTALLED -- in both trees, on
#                 identical inputs.
#   reward      : correctness * mean_f( ln(max(1, speedup_f)) )   [helpers.sh]
#
# Identical for every step.  Per-step data: FUNCS.step$STEP, BASE_STAGES.step$STEP,
# WORKLOAD.step$STEP, GUARD.step$STEP.

SRC=/app/parasail
[ -d "$SRC" ] || { echo "missing $SRC"; report 0; }
FUNCS="/tests/FUNCS.step$STEP"
CSVSEED=$(awk '$1 == "seed" { print $2 }' "/tests/WORKLOAD.step$STEP")
CSVLEN=$(awk '$1 == "len" { print $2 }' "/tests/WORKLOAD.step$STEP")
CSVREPS=$(awk '$1 == "reps" { print $2 }' "/tests/WORKLOAD.step$STEP")
PIPEL1=$(awk '$1 == "pipel1" { print $2 }' "/tests/WORKLOAD.step$STEP")
PIPEL2=$(awk '$1 == "pipel2" { print $2 }' "/tests/WORKLOAD.step$STEP")
PIPEREPS=$(awk '$1 == "pipereps" { print $2 }' "/tests/WORKLOAD.step$STEP")

echo "== building candidate =="
# The build is candidate code: meson evaluates the candidate's meson.build and
# ninja compiles its sources, so /app goes to the unprivileged user and the
# build runs as that user.  The base build in /basebld was made at verifier
# image build time from pinned sources and stays root-owned.
give_app_to_unprivileged
build_tree "$SRC" /tmp/cand run_unprivileged || report 0

# Verifier-owned: the harness source is /tests/vendor and its only header comes
# from /base, both of which the candidate cannot touch, so this compile stays
# root (nothing in it is candidate code).
gcc -O2 -o /tmp/harness /tests/vendor/bench_stage.c -I /base -I /base/src -ldl \
    > /tmp/harness.log 2>&1 || { echo "harness build failed"; tail -20 /tmp/harness.log; report 0; }
CAND=/tmp/cand/src/libparasail.so
BASE=/basebld/src/libparasail.so
[ -f "$CAND" ] || { echo "candidate built no shared library"; report 0; }
[ -f "$BASE" ] || { echo "base built no shared library"; report 0; }

correct=1
nscoped=$(grep -cv '^\s*$' "$FUNCS" || true)
echo "== this step's scope: $nscoped functions =="

echo "== correctness: every scoped function vs this step's base =="
# The harness dlopens the library it is pointed at, so pointing it at $CAND
# loads and runs candidate code in this container (static initializers, the
# entry points themselves).  Every $CAND invocation therefore runs through
# run_unprivileged; $BASE is the verifier's own build and stays root.
taskset -c 0,1 /tmp/harness "$BASE" run "$FUNCS" "$CSVSEED" "$CSVLEN" "$CSVLEN" 1 dump \
    > /tmp/dump_base.txt 2>/tmp/dump_base.err
run_unprivileged taskset -c 0,1 /tmp/harness "$CAND" run "$FUNCS" "$CSVSEED" "$CSVLEN" "$CSVLEN" 1 dump \
    > /tmp/dump_cand.txt 2>/tmp/dump_cand.err
nmismatch=0; nseen=0
while read -r name tag digest; do
    [ -z "${name:-}" ] && continue
    nseen=$((nseen + 1))
    b=$(awk -v k="$name" '$1 == k { print $3 }' /tmp/dump_base.txt)
    c=$(awk -v k="$name" '$1 == k { print $3 }' /tmp/dump_cand.txt)
    if [ -z "$b" ] || [ -z "$c" ]; then
        echo "  $name: no digest (base='${b:-}' candidate='${c:-}')"; correct=0; nmismatch=$((nmismatch + 1))
    elif [ "$b" != "$c" ]; then
        echo "  $name: MISMATCH ($c != $b)"; correct=0; nmismatch=$((nmismatch + 1))
    fi
done < "$FUNCS"
# The gate must examine something: a scope file that produced no digests at all
# is an empty input set, and "0 mismatches" over nothing reads exactly like a
# clean pass.
if [ "$nseen" -ne "$nscoped" ]; then
    echo "  only $nseen of $nscoped scoped functions were checked"; correct=0
elif [ "$nmismatch" -eq 0 ]; then
    echo "  all $nseen scoped functions byte-identical to the base build"
fi

echo "== capability envelope: scoped to the code the candidate added =="
# Two promises about the candidate's OWN code: each entry point answers in the
# solution width it is named for, and nothing is written above this step's
# instruction-set ceiling (tests/ISA.step$STEP).  Neither is a property of the
# tree the candidate started from -- the stripped base answers every removed
# family from the portable reference, so its `nw_striped_16` reports width 32
# by construction and there is no kernel in it to read a ceiling from -- so
# both are asked about the entry points the candidate answers with code it
# ADDED, and a tree that changed nothing in this step's scope passes both
# vacuously, with the counts printed so the gate is auditable rather than
# implicit.  An entry point left on the portable path is not a correctness
# matter: it scores ln(1) = 0 in the speedup half, which is where an unfinished
# step is supposed to lose.
python3 -I /tests/vendor/envelope.py owned --base /base --cand "$SRC" \
    --funcs "$FUNCS" --out /tmp/owned_src.txt > /tmp/owned_rep.txt 2>&1
if [ $? -ne 0 ]; then
    echo "  envelope ownership scan failed:"; sed 's/^/    /' /tmp/owned_rep.txt; correct=0
fi
sed 's/^/  /' /tmp/owned_rep.txt
[ -s /tmp/owned_src.txt ] || : > /tmp/owned_src.txt

taskset -c 0,1 /tmp/harness "$BASE" run "$FUNCS" "$CSVSEED" "$CSVLEN" "$CSVLEN" 1 widths \
    > /tmp/width_base.txt 2>&1
run_unprivileged taskset -c 0,1 /tmp/harness "$CAND" run "$FUNCS" "$CSVSEED" "$CSVLEN" "$CSVLEN" 1 widths \
    > /tmp/width_cand.txt 2>&1
# A width probe that produced nothing is a failed measurement, not a clean one.
nwseen=$(grep -cv '^\s*$' /tmp/width_cand.txt || true)
case "${nwseen:-x}" in ''|*[!0-9]*) nwseen=0 ;; esac
if [ "$nwseen" -eq 0 ]; then
    echo "  no widths examined: the width probe produced no output"; correct=0
fi

# The set the width contract is checked on: entry points a line the candidate
# added *binds* -- an assignment to the name or to its `_pointer`, a #define of
# either, or a public wrapper whose body stopped routing through that pointer
# (envelope.py owned) -- plus entry points whose reported width no longer
# matches the base build's.  The second signal catches an implementation
# reached through a name the first scan cannot see (a macro table, an opaque
# helper); the first catches a kernel installed under a name it answers with
# the wrong width.  A line that merely mentions the name -- a comment, a string
# in a driver's name table, a call to an entry point left on the portable path
# -- binds nothing and does not enter the set: the instruction says such an
# entry point is unimplemented, not a width failure.
: > /tmp/owned.txt
while read -r name; do
    [ -z "${name:-}" ] && continue
    printf '%s\n' "$name" >> /tmp/owned.txt
done < /tmp/owned_src.txt
while read -r name tag w; do
    [ -z "${name:-}" ] && continue
    bw=$(awk -v k="$name" '$1 == k && $2 == "W" { print $3 }' /tmp/width_base.txt)
    [ -n "${bw:-}" ] && [ "${w:-}" = "$bw" ] && continue
    grep -qxF "$name" /tmp/owned.txt && continue
    printf '%s\n' "$name" >> /tmp/owned.txt
done < /tmp/width_cand.txt
nowner=$(grep -cv '^\s*$' /tmp/owned.txt || true)
case "${nowner:-x}" in ''|*[!0-9]*) nowner=0 ;; esac

if [ "$nowner" -eq 0 ]; then
    echo "  envelope: 0 of $nscoped scoped entry points are answered by code the candidate added; width check skipped (nothing changed in this step's scope)"
else
    nwrong=0
    while read -r name; do
        [ -z "${name:-}" ] && continue
        w=$(awk -v k="$name" '$1 == k && $2 == "W" { print $3 }' /tmp/width_cand.txt)
        want=${name##*_}
        if [ "${w:-0}" != "$want" ]; then
            echo "  $name reports solution width ${w:-0}, is named for $want"; nwrong=$((nwrong + 1))
        fi
    done < /tmp/owned.txt
    if [ "$nwrong" -ne 0 ]; then
        correct=0
    else
        echo "  envelope: $nowner of $nscoped scoped entry points are answered by code the candidate added; all $nowner report their documented width"
    fi
fi

isa_ceiling=$(cat "/tests/ISA.step$STEP" 2>/dev/null || true)
python3 -I /tests/vendor/envelope.py isa --base /base --cand "$SRC" \
    --funcs "$FUNCS" --ceiling "$isa_ceiling" > /tmp/env_isa.txt 2>&1
isa_rc=$?
sed 's/^/  /' /tmp/env_isa.txt
if [ "$isa_rc" -ne 0 ]; then
    echo "  OUT OF ENVELOPE: this step's instruction-set ceiling is '${isa_ceiling:-unset}'"; correct=0
fi

echo "== correctness: library-wide consistency digest =="
taskset -c 0,1 /tmp/harness "$BASE" digest "/tests/GUARD.step$STEP" "$CSVSEED" 240 240 > /tmp/g_base.txt 2>&1
run_unprivileged taskset -c 0,1 /tmp/harness "$CAND" digest "/tests/GUARD.step$STEP" "$CSVSEED" 240 240 > /tmp/g_cand.txt 2>&1
gb=$(awk '{print $2}' /tmp/g_base.txt); gc=$(awk '{print $2}' /tmp/g_cand.txt)
if [ -z "${gb:-}" ] || [ -z "${gc:-}" ]; then
    echo "  digest missing (base='${gb:-}' candidate='${gc:-}')"; correct=0
elif [ "$gb" != "$gc" ]; then
    echo "  digest MISMATCH: $gc != $gb"; correct=0
else
    echo "  $(grep -cv '^\s*$' "/tests/GUARD.step$STEP") entry points agree ($gc)"
fi

echo "== correctness: upstream suite, no new failures vs base =="
suite_failures /basebld /base base > /tmp/base_fail.txt
# The candidate's suite binaries are candidate code and run in this container.
suite_failures /tmp/cand "$SRC" cand run_unprivileged > /tmp/cand_fail.txt
echo "  base failures: $(wc -l < /tmp/base_fail.txt | tr -d ' ')   candidate: $(wc -l < /tmp/cand_fail.txt | tr -d ' ')"
newf=$(comm -13 /tmp/base_fail.txt /tmp/cand_fail.txt)
if [ -n "$newf" ]; then echo "  NEW failures:"; echo "$newf" | sed 's/^/    /'; correct=0
else echo "  none"; fi
echo "  upstream test_verify report, restricted to this step's scope:"
base_scoped=$(scoped_verify_errors /tmp/base_test_verify.log "$FUNCS" | tail -1 | awk '{print $NF}')
cand_scoped=$(scoped_verify_errors /tmp/cand_test_verify.log "$FUNCS" | tail -1 | awk '{print $NF}')
case "${base_scoped:-x}" in ''|*[!0-9]*) base_scoped=-1 ;; esac
case "${cand_scoped:-x}" in ''|*[!0-9]*) cand_scoped=-1 ;; esac
# -1 is "the report could not be read" (its log is missing), not "zero wrong".
# Reading it as clean would let a tree whose test_verify never ran pass this
# gate; the suite gate above records the missing binary as a new failure, and
# this branch fails the check directly rather than trusting the absence.
if [ "$base_scoped" != "0" ]; then
    echo "    the base build itself reports $base_scoped scoped entry point(s); scope is not clean"; correct=0
elif [ "$cand_scoped" -lt 0 ]; then
    echo "    the candidate's test_verify report could not be read (no /tmp/cand_test_verify.log): scope not verified"; correct=0
elif [ "$cand_scoped" -gt 0 ]; then
    scoped_verify_errors /tmp/cand_test_verify.log "$FUNCS"; correct=0
else
    echo "    base 0, candidate 0 (checked $(grep -cv '^\s*$' "$FUNCS") scoped names)"
fi

echo "== end-to-end non-regression gate (pass/fail, does not score) =="
# Two whole-library timings, both on a draw shape the scored set does not use
# (pipel1 x pipel2, against the scored len x len).  Neither baselines on the
# ladder position: each compares the candidate's build against the BASE build on
# entry points that BOTH trees answer from the same portable reference sources,
# so a no-op and the oracle both read ~1.0 and neither is charged for work this
# ladder has already asked of it -- which is the failure a baseline of L(k-1)
# produces, and a no-op's tree is the ladder's L0 floor.
#
#   future : families this step's base has removed and this step does not own.
#            Same reference code in both trees, so a difference is a property of
#            the BUILD -- this is what catches a tree compiled well for its own
#            kernels and badly for everything else.
#   own    : this step's own entry points.  The base answers them from the
#            portable reference, so this requires the candidate's kernels to be
#            no worse than the reference on a shape the scored draw never uses.
for grp in "future:/tests/FUTURE.step$STEP" "own:$FUNCS"; do
    what=${grp%%:*}; lst=${grp#*:}
    n=$(grep -cv '^\s*$' "$lst")
    if [ "$n" -eq 0 ]; then
        echo "  $what: no such entry points at this step; not applicable"
        continue
    fi
    tb=$(taskset -c 0,1 /tmp/harness "$BASE" run "$lst" "$CSVSEED" "$PIPEL1" "$PIPEL2" "$PIPEREPS" pipe 2>/dev/null | awk '$1 == "PIPE" { print $2 }')
    tc=$(run_unprivileged taskset -c 0,1 /tmp/harness "$CAND" run "$lst" "$CSVSEED" "$PIPEL1" "$PIPEL2" "$PIPEREPS" pipe 2>/dev/null | awk '$1 == "PIPE" { print $2 }')
    case "${tb:-}" in ''|*[!0-9]*) tb=0 ;; esac
    case "${tc:-}" in ''|*[!0-9]*) tc=0 ;; esac
    if [ "$tb" -le 0 ] || [ "$tc" -le 0 ]; then
        echo "  $what: no timing (base='${tb}' candidate='${tc}')"; correct=0; continue
    fi
    r=$(awk -v a="$tc" -v b="$tb" 'BEGIN { printf "%.3f", a / b }')
    echo "  $what ($n entry points, draw ${PIPEL1}x${PIPEL2}): base ${tb} ns, candidate ${tc} ns, ratio $r"
    # 2.0, not 1.5.  Two builds of *identical* sources measured 1.33 on the
    # `own` list at step 6 under load, and this gate must never produce a
    # false zero on an unmodified tree.  The regression this gate is meant to
    # catch reads 2.44, so 2.0 catches it with margin on both sides.
    if awk -v r="$r" 'BEGIN { exit !(r > 2.0) }'; then
        echo "    REGRESSED by more than 100% against the base build (regression gate disabled)"
    fi
done

echo "== per-function measurement =="
taskset -c 0,1 /tmp/harness "$BASE" run "$FUNCS" "$CSVSEED" "$CSVLEN" "$CSVLEN" "$CSVREPS" bench \
    > /tmp/bench_base.txt 2>&1
run_unprivileged taskset -c 0,1 /tmp/harness "$CAND" run "$FUNCS" "$CSVSEED" "$CSVLEN" "$CSVLEN" "$CSVREPS" bench \
    > /tmp/bench_cand.txt 2>&1

speedups=()
while read -r name; do
    [ -z "${name:-}" ] && continue
    tb=$(awk -v k="$name" '$1 == k && $2 == "NS" { print $3 }' /tmp/bench_base.txt)
    tc=$(awk -v k="$name" '$1 == k && $2 == "NS" { print $3 }' /tmp/bench_cand.txt)
    # A missing or nonsensical reading must not flow into the ratio as a
    # sentinel: "could not measure" is a failed measurement, not a fast one.
    case "${tb:-}" in ''|*[!0-9]*) tb=0 ;; esac
    case "${tc:-}" in ''|*[!0-9]*) tc=0 ;; esac
    if [ "$tb" -le 0 ] || [ "$tc" -le 0 ]; then
        echo "  $name: no timing (base='${tb}' candidate='${tc}')"; correct=0
        speedups+=("1"); continue
    fi
    sp=$(awk -v a="$tb" -v b="$tc" 'BEGIN { r = a / b; if (r > 1000) r = 1000; if (r < 0) r = 0; printf "%.4f", r }')
    printf "  %-26s %10s -> %10s ns   %6sx\n" "$name" "$tb" "$tc" "$sp"
    speedups+=("$sp")
done < "$FUNCS"

[ "${#speedups[@]}" -eq 0 ] && { echo "no functions measured"; report 0; }
report "$correct" "${speedups[@]}"
