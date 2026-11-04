#!/usr/bin/env bash

# --- verifier/step binding check -------------------------------------------
# /etc/harbor-step-id names the image; the literal below names this tests/ tree.
# They disagree only when the build context came from another step's cache
# entry, in which case every check below would grade the wrong milestone.
# Reported, not crashed: a non-zero exit makes Harbor skip the remaining steps
# and buries the fault under a second symptom.
__hb_want=05
__hb_fault=''
__hb_got=$(cat /etc/harbor-step-id 2>/dev/null) || __hb_got=MISSING
if [ "$__hb_got" != "$__hb_want" ]; then
    __hb_fault="image is step ${__hb_got}, /tests tree is step ${__hb_want}"
    __hb_kind=step_mismatch
fi

# The per-step data files decide which milestone is graded: FUNCS is the scored
# set, ISA the instruction-set envelope, E2E_TOL the end-to-end budget and STAGE
# labels it. test.sh's own identity being right does not make THEM right --
# they arrive through a different mechanism (a COPY from the build context,
# rather than this file's literal). Their digests are baked into the image by a
# RUN, which is keyed on its command text and so cannot be served from a sibling
# step's cache.
if [ -z "$__hb_fault" ]; then
    if [ ! -r /etc/harbor-step-datasums ]; then
        __hb_fault="/etc/harbor-step-datasums is missing from this image"
        __hb_kind=datasums_missing
    elif ! ( cd /tests && sha256sum -c /etc/harbor-step-datasums ) \
             > /tmp/hb-datasums.txt 2>&1; then
        __hb_fault="per-step data files do not match step ${__hb_want}: $(
            grep -v ': OK$' /tmp/hb-datasums.txt | tr '\n' ' ')"
        __hb_kind=data_mismatch
    fi
fi

if [ -n "$__hb_fault" ]; then
    __hb_msg="VERIFIER/STEP MISMATCH: ${__hb_fault}."
    printf '%s\n' "$__hb_msg" >&2
    mkdir -p /logs/verifier 2>/dev/null
    printf '%s\n' "$__hb_msg" > /logs/verifier/step-mismatch.log
    printf '{"reward":0,"verifier_error":"%s","image_step":"%s","tests_step":"%s"}\n' \
        "$__hb_kind" "$__hb_got" "$__hb_want" > /logs/verifier/reward.json
    exit 0
fi
unset __hb_want __hb_got __hb_msg __hb_fault __hb_kind
# --- end binding check ------------------------------------------------------
# context-uniqueness padding ...............................................................................................
set -u
source /tests/helpers.sh

# Lock the reward directory before anything executes candidate code. Harbor
# chmods /logs/verifier to 777 just before this container starts, so candidate
# code the verifier builds or runs could otherwise overwrite reward.json -- a
# detached child would even outlive the scorer. Everything below that executes
# candidate code runs as nobody (CANDIDATE_UNPRIV, set after the base has been
# measured below), which is what makes this mean something: root can write a
# root-owned directory whatever its mode, nobody cannot.
harden_reward_dir

# Runs in this step's verifier container, with the candidate's cumulative
# workspace mounted at /app.
#
#   speedup     : the GEOMETRIC MEAN of the per-function speedups over exactly
#                 this step's scored functions (tests/FUNCS). For each one,
#                 FFmpeg's own checkasm benchmark reports the cycles per call of
#                 the implementation the library installs at the baseline CPU
#                 level -- the portable C reference -- and of the one it installs
#                 on this host. No whole-program timing, so no dilution.
#
#   correctness : binary, and NARROW first -- the step is graded on doing its own
#                 job. The scored gate is checkasm's per-function differential
#                 over the same scored functions, on the same randomized inputs
#                 the benchmark times, at upstream's own per-function bound.
#                 Three regression guards sit beside it and can also zero the
#                 reward, so a step cannot trade an earlier stage's correctness
#                 for its own speed: the whole libswscale checkasm suite
#                 relative to the base's recorded failures, the end-to-end
#                 sws_scale output within a declared budget, and an end-to-end
#                 non-regression timing check against the ladder's L0 floor.
#
#   reward      : correctness * ln(max(1, speedup))   (see helpers.sh)
#
#   isa_objects : the number of object files the instruction-set envelope gate
#                 followed this milestone's dispatch to, and examined. Reported
#                 so that a gate which examined nothing is visible; 0 is either
#                 that failure or the gate being skipped because this
#                 milestone's own files are unchanged, and the envelope line
#                 printed above says which.
#
# This file is identical for every step. Per-step data: tests/FUNCS,
# tests/ISA, tests/E2E_TOL, tests/STAGE.

STAGE=$(cat /tests/STAGE)
SRC=/app/ffmpeg
[ -d "$SRC" ] || { echo "missing $SRC"; report 0 0; }
correct=1

echo "== integrity: nothing changed outside libswscale/x86/ =="
# The scoring tree below is assembled from the base plus the candidate's
# libswscale/x86/, so the measurement no longer depends on this check. It stays
# because a tree that edits the portable C, the checkasm framework or the build
# system is not doing the task -- and because the portable C is the reference
# every per-function ratio is divided by, so slowing it down would inflate every
# number.
if ! python3 -I /tests/vendor/fence.py /base "$SRC" > /tmp/fence.txt 2>&1; then
    echo "  files changed outside libswscale/x86/:"
    sed 's/^/    /' /tmp/fence.txt | head -40
    correct=0
else
    echo "  tree unchanged outside libswscale/x86/"
fi

echo "== baseline: the ladder's base, measured in this container =="
# /work/ffmpeg and /work/bld hold the base right now; they are grafted below.
# The baseline is measured here rather than at image build so that it sees the
# same machine, the same cores and the same run as the candidate.
assert_checkasm_live /work/bld || report 0 0
bench_stage /work/bld /tmp/base-bench.1 11
bench_stage /work/bld /tmp/base-bench.2 12
echo "  base failure list: $(wc -l < /base_checkasm_failures.txt) recorded"

echo "== building the scoring tree =="
# The base, with only the candidate's libswscale/x86/ grafted on. Everything
# that decides or measures anything -- the portable C references, checkasm and
# its framework, the build system -- is therefore the verifier's own copy.
rm -rf /work/ffmpeg/libswscale/x86
cp -a "$SRC/libswscale/x86" /work/ffmpeg/libswscale/x86
# Stale objects from the base's build would otherwise stay in the archive and
# still be linked: `ar r` does not remove members that are no longer listed.
rm -f /work/bld/libswscale/x86/*.o /work/bld/libswscale/libswscale.a
# From here on the tree under test contains the candidate's code, so every
# command that executes it runs as nobody: the build (a Makefile is a program),
# checkasm and the measurement harness (both link candidate objects, whose
# static initializers run at load). The base measured above is the verifier's
# own tree and stays root. build_tree prepares the build tree for the
# unprivileged user before it builds, and retries with the source tree handed
# over if the build turns out to need it.
CANDIDATE_UNPRIV=1
build_tree /work/ffmpeg /work/bld || report 0 0
assert_checkasm_live /work/bld || report 0 0
build_harness /work/ffmpeg /work/bld /h-cand || report 0 0

echo "== scored gate: per-function differential over this milestone =="
checkasm_log /work/bld /tmp/cand.log 11 > /dev/null
checkasm_failures /tmp/cand.log > /tmp/cand_failures.txt
scoped_failures=$(comm -12 /tmp/cand_failures.txt \
    <(grep . /tests/FUNCS | sort -u))
scope_count=$(grep -c . /tests/FUNCS)
# The benchmark table is the evidence the scored gate examined anything; the
# per-group OK lines go to stderr.
tested_count=$(awk '/^Benchmark results/{f=1;next} f && /^  [A-Za-z0-9_]/ {n++} END{print n+0}' /tmp/cand.log)
echo "  $scope_count scored functions; $tested_count benchmark rows in this run"
if [ "$tested_count" -eq 0 ]; then
    echo "  checkasm produced no benchmark rows -- the scored gate examined nothing"
    correct=0
fi
if [ -n "$scoped_failures" ]; then
    echo "  THIS MILESTONE'S OWN FUNCTIONS FAIL against the C reference:"
    echo "$scoped_failures" | sed 's/^/    /' | head -20
    correct=0
else
    echo "  no failure inside this milestone's scored set"
fi

echo "== guard: whole libswscale checkasm suite (no new failures vs base) =="
new_failures=$(comm -23 /tmp/cand_failures.txt /base_checkasm_failures.txt)
if [ -n "$new_failures" ]; then
    echo "  NEW checkasm failures outside this milestone's scope:"
    echo "$new_failures" | sed 's/^/    /' | head -20; correct=0
else
    echo "  no new checkasm failures"
fi

echo "== guard: end-to-end sws_scale output within the declared budget =="
rm -rf /tmp/cand-dump
e2e_dump /h-cand /tmp/cand-dump || { echo "  the end-to-end driver did not run"; correct=0; }
python3 -I /tests/vendor/e2e.py /base-dump /tmp/cand-dump /tests/E2E_TOL \
    || { echo "  end-to-end output exceeded the declared budget"; correct=0; }
# Determinism: the same build run twice must produce the same frame. A kernel
# that reads uninitialised state passes a single-shot comparison and fails this.
e2e_dump /h-cand /tmp/cand-dump2 || { echo "  the end-to-end driver did not run"; correct=0; }
if ! diff -rq /tmp/cand-dump /tmp/cand-dump2 > /dev/null 2>&1; then
    echo "  the pipeline is not deterministic across runs"; correct=0
else
    echo "  deterministic across repeated runs"
fi

echo "== envelope: instruction set of the kernels this milestone installs =="
# With `enforce no` the gate stands on a statement about the machine -- the
# host executes nothing wider than this milestone's ceiling, so there is
# nothing narrower to check -- and nothing else asserts that statement. Say one
# line about the premise every run, so a skipped envelope is visible rather
# than inferred, and so a host wider than the ceiling is named instead of
# silently crediting access to it. Reported, not scored: see check_host_ceiling
# for why enforcement is not the cure.
check_host_ceiling /tests/ISA
# The envelope is a fairness rule about code the candidate wrote, so the gate
# is scoped to what the candidate changed. A tree whose milestone files are all
# byte-identical to the base's is the tree the base's own behaviour was
# recorded on: it cannot have brought wider registers than the reference, so
# the gate is vacuous for it -- and says so, rather than passing on an
# examination that found nothing. A tree that changed any file this milestone
# owns is checked exactly as before.
isa_objects=0
python3 -I /tests/vendor/envelope_scope.py "$SRC/libswscale/x86" \
    /base/libswscale/x86 /tests/ISA > /tmp/envelope-scope.txt 2>&1
scope_rc=$?
sed 's/^/  /' /tmp/envelope-scope.txt | grep -vE '^  (SCOPED|UNCHANGED) '
scope_total=$(awk '/^SCOPED /{print $2}' /tmp/envelope-scope.txt)
scope_unchanged=$(awk '/^UNCHANGED /{print $2}' /tmp/envelope-scope.txt)
if [ "$scope_rc" -ne 0 ]; then
    echo "  the envelope's scope could not be determined (envelope_scope.py"
    echo "  exited $scope_rc) -- which file a candidate changed is not a question"
    echo "  this verifier can leave open"
    correct=0
elif [ -n "$scope_total" ] && [ "${scope_unchanged:-0}" -eq "$scope_total" ]; then
    echo "  envelope: $scope_unchanged of $scope_total scoped files unchanged by"
    echo "  the candidate -- check skipped"
else
    echo "  envelope: ${scope_unchanged:-0} of ${scope_total:-0} scoped files unchanged by the candidate -- check runs"
    isa_examined /work/bld /tests/ISA /work/ffmpeg/libswscale/x86 > /tmp/isa.txt 2>&1 \
        || { echo "  the instruction-set envelope check FAILED -- see above"; correct=0; }
    # Two counts, because they mean different things: how many objects exist, and
    # how many the milestone's own dispatch was followed to. The second is the one
    # the envelope is enforced over, and it is what gets reported. A scope that
    # resolves to nothing at all is a failure, not a pass, however many objects
    # were built -- and the total is asserted non-zero for the same reason.
    isa_total=$(awk '/^  TOTAL_OBJECTS /{print $2}' /tmp/isa.txt)
    isa_scope=$(awk '/^  MILESTONE_OBJECTS /{print $2}' /tmp/isa.txt)
    isa_objects="${isa_scope:-0}"
    sed 's/^/  /' /tmp/isa.txt | head -5
    # The violations are printed after the scope list, which the head above
    # truncates -- but they are the one line a red run needs to show, so they
    # are echoed even when the list is not.
    grep 'VIOLATION' /tmp/isa.txt | sed 's/^/  /'
    if [ -z "$isa_total" ] || [ "$isa_total" -eq 0 ]; then
        echo "  the envelope check found no built objects to examine"
        correct=0
    fi
    if [ "$(cat /tests/ISA | awk '$1=="enforce"{print $2}')" = "yes" ] && \
       [ "${isa_scope:-0}" -eq 0 ]; then
        echo "  the scoped envelope gate followed this milestone's dispatch to no"
        echo "  object at all -- it examined nothing, which is a failure"
        correct=0
    fi
fi

echo "== speedup: per-function over $scope_count scored functions =="
bench_stage /work/bld /tmp/cand-bench.1 11
bench_stage /work/bld /tmp/cand-bench.2 12
python3 -I /tests/vendor/bench.py --funcs /tests/FUNCS \
    --cand /tmp/cand-bench.1 --cand /tmp/cand-bench.2 \
    --base /tmp/base-bench.1 --base /tmp/base-bench.2 \
    --stderr /tmp/cand.log.err > /tmp/bench.txt 2>&1
sed 's/^/  /' /tmp/bench.txt | grep -vE '^  (SPEEDUP|MEAN_LOG)'
speedup=$(awk '/^SPEEDUP/{print $2}' /tmp/bench.txt)
[ -n "$speedup" ] || speedup=0
unmeasured=$(awk '/^UNMEASURED/{print $2}' /tmp/bench.txt)

echo "== guard: a microbenchmark win must not cost the real pipeline =="
# Pass/fail, never scored, and baselined on the LADDER'S L0 FLOOR rather than on
# this step's own base: a no-op agent's tree is L0 at every step, so a guard
# baselined on the previous level reports a no-op as catastrophically slow and
# asks the wrong question. L0 is a floor every candidate starts from, so the
# question this asks is the intended one -- did the tree as a whole get
# materially slower than the tree it was built from?
if [ "$correct" = 1 ]; then
    e2e_time_pair /h-base /h-cand 5 > /tmp/pair-time.txt \
        || { echo "  end-to-end timing failed"; correct=0; }
    awk '{print $1, $2}' /tmp/pair-time.txt > /tmp/base-time.txt
    awk '{print $1, $3}' /tmp/pair-time.txt > /tmp/cand-time.txt
    ratio=$(python3 -I /tests/vendor/e2e_ratio.py /tmp/base-time.txt /tmp/cand-time.txt)
    echo "  candidate/base end-to-end ratio $ratio (limit 1.75, lower is better;"
    echo "  both sides timed interleaved in the same rounds)"
    if awk -v r="$ratio" 'BEGIN{exit !(r > 1.75)}'; then
        echo "  END-TO-END REGRESSION: sws_scale as a whole got materially slower."
        correct=0
    fi
fi

echo "  unscored diagnostics: speedup=$speedup unmeasured=${unmeasured:-[]}"
report "$correct" "$speedup" "${isa_objects:-0}"
