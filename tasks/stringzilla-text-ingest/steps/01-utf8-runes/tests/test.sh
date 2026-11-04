#!/usr/bin/env bash

# --- verifier/step binding check -------------------------------------------
# /etc/harbor-step-id names the image; the literal below names this tests/ tree.
# They disagree only when the build context came from another step's cache
# entry, in which case every check below would grade the wrong milestone.
# Reported, not crashed: a non-zero exit makes Harbor skip the remaining steps
# and buries the fault under a second symptom.
__hb_step=01
__hb_got=$(cat /etc/harbor-step-id 2>/dev/null) || __hb_got=MISSING
if [ "$__hb_got" != "$__hb_step" ]; then
    __hb_msg="VERIFIER/STEP MISMATCH: image is step ${__hb_got}, /tests tree is step ${__hb_step}."
    printf '%s\n' "$__hb_msg" >&2
    mkdir -p /logs/verifier 2>/dev/null
    printf '%s\n' "$__hb_msg" > /logs/verifier/step-mismatch.log
    printf '{"reward":0,"verifier_error":"step_mismatch","image_step":"%s","tests_step":"%s"}\n' \
        "$__hb_got" "$__hb_step" > /logs/verifier/reward.json
    exit 0
fi
# --- end binding check ------------------------------------------------------
set -u
source /tests/helpers.sh

# --- reward directory -------------------------------------------------------
# Harbor chmods /logs/verifier to 777 just before this container starts, so
# anything running here can overwrite reward.json -- including the candidate
# code the checks below configure, compile and run. Writing the real score last
# does not help: a detached child outlives the scorer and writes after it.
# Take the directory back to root:root 0700 first, and run every candidate
# program as nobody (run_unprivileged in helpers.sh) so it cannot undo this.
# `report` reopens the directory to 0755 on the way out; it stays root-owned.
harden_reward_dir

# Runs in this step's verifier container on a fresh copy of /app.
#
#   correctness : 1 only if the candidate's library still builds, its public API
#                 still compiles, every scoped function reproduces the base
#                 build's output byte for byte on identical generated inputs,
#                 the project's own suite gains no new failure, whatever the
#                 candidate wrote in this milestone's own files stays inside the
#                 capability envelope, the workspace handoff is still in place,
#                 and the whole ingest pass has not regressed past the L0 floor.
#   speedup     : one ratio per scoped function, from a harness compiled
#                 against each tree's own headers, so each side runs the
#                 implementation that tree installs.
#   reward      : correctness * mean_f( ln(max(1, speedup_f)) )  [helpers.sh]
#
# Identical for every step. Per-step data: tests/FUNCS.stepNN, tests/SCOPE.stepNN.

SRC=/app/stringzilla
FUNCS=/tests/FUNCS.step${__hb_step}
SCOPE=$(cat /tests/SCOPE.step${__hb_step})
[ -d "$SRC" ] || { echo "missing $SRC"; report 0; }

correct=1
speedups=()

# --- the workspace handoff is preserved -------------------------------------
# §3 of the instruction requires the handoff documents to be preserved, and
# /app/WORKSPACE_HANDOFF.md ships with the workspace.  This is what enforces
# it.  The check reads the artifact as the agent left it and runs before any
# candidate code (the build below), because the build cannot be trusted not to
# touch it.  An agent may append to the handoff or leave it exactly as it is;
# nothing in its contents is parsed, and none of it is scored.
HANDOFF=/app/WORKSPACE_HANDOFF.md
if [ -s "$HANDOFF" ]; then
    echo "== the workspace handoff is in place ($(wc -c < "$HANDOFF") bytes) =="
else
    echo "== the workspace handoff is missing or empty: $HANDOFF =="
    correct=0
fi

echo "== correctness: the library builds with its own suite enabled =="
# Everything below that runs this tree's own code -- the CMake configure, the
# compiler, the built suite, the harness compiled against its headers, every
# timed call -- runs as nobody, so candidate code can never write
# /logs/verifier.  /app is handed to that uid first because a build may write
# into the source tree, and the scratch directories are created for it: a build
# that cannot create its output directory would score every candidate 0 and
# look merely very hard instead of broken.
rm -rf /tmp/cand /tmp/nb /tmp/h_cand /tmp/cmake.log /tmp/build.log /tmp/harness.log
mkdir -p /tmp/cand /tmp/nb
chown -R "$SCORE_UID:$SCORE_GID" /tmp/cand /tmp/nb 2>/dev/null || true
give_app_to_unprivileged
if build_candidate "$SRC" /tmp/cand; then echo "  built"; else correct=0; fi

echo "== correctness: the public API still compiles against this tree =="
if [ "$correct" = 1 ] && ! build_harness "$SRC" /tmp/h_cand; then
    echo "  a public entry point no longer compiles"; correct=0
fi

if [ "$correct" = 1 ]; then
    echo "== correctness: capability envelope, scoped to what the candidate wrote =="
    # The envelope says "the candidate's kernels must not reach for a machine
    # the reference never had", so it is checked on the files this step owns
    # that the candidate actually added or changed.  /base is this step's base
    # tree, built in this image from the pin; a scoped file byte-identical to
    # it is upstream's code, not the candidate's, and check_isa says so and
    # skips it rather than reporting a tree that changed nothing as having
    # changed behaviour.  A milestone whose own files were written is scanned
    # exactly as before, and a deleted scoped file is reported -- the scan can
    # never pass by examining nothing.
    source /tests/check_isa.sh
    check_isa "$SRC" "$SCOPE" /base || correct=0
fi

if [ "$correct" = 1 ]; then
    echo "== correctness: upstream suite, no new failures vs this step's base =="
    # The suite binary is built from candidate source, so ctest runs it as
    # nobody; it writes its own logs under the (nobody-owned) build directory.
    run_unprivileged ctest --test-dir /tmp/cand -R stringzilla_test_cpp20 > /tmp/ctest.log 2>&1 || true
    sed -n '/tests FAILED/,$p' /tmp/ctest.log \
      | grep -oE '^[[:space:]]+[0-9]+ - [^ ]+' | awk '{print $3}' | sort -u > /tmp/cand_fail.txt
    newf=$(comm -23 /tmp/cand_fail.txt /base_ctest_failures.txt)
    if [ -n "$newf" ]; then echo "  NEW failures:"; echo "$newf" | sed 's/^/    /'; correct=0
    else echo "  none (this step's base already fails $(wc -l < /base_ctest_failures.txt))"; fi

    # Test integrity.  The list above is baseline-relative, so a test that was
    # deleted, renamed or filtered out of the run cannot produce a failure and
    # cannot be seen there.  The suite's runner prints one "- <name> ..." line
    # per test it executes, so the candidate's binary is re-run the way the
    # image recorded the base's: the runner's filter knobs unset, output read
    # directly because ctest hides a passing test's.  Every test this step's
    # base executed must still execute, and a suite that passes in the base
    # must not fail here.
    cand_bin=$(find /tmp/cand -maxdepth 3 -name stringzilla_test_cpp20 -type f -perm -u+x | head -1)
    if [ -z "$cand_bin" ]; then
        echo "  the suite binary is missing from the candidate build"; correct=0
    else
        run_unprivileged env -u SZ_TESTS_FILTER -u SZ_TESTS_MULTIPLIER -u SZ_TESTS_SEED \
            "$cand_bin" > /tmp/cand_suite.log 2>&1; cand_rc=$?
        sed -n 's/^- \(.*\) \.\.\.$/\1/p' /tmp/cand_suite.log | sort -u > /tmp/cand_suite_inventory.txt
        if [ ! -s /tmp/cand_suite_inventory.txt ]; then
            echo "  the suite executed no tests at all"; correct=0
        else
            gone=$(comm -23 /base_suite_inventory.txt /tmp/cand_suite_inventory.txt)
            if [ -n "$gone" ]; then
                echo "  tests this step's base ran and this tree does not:"
                echo "$gone" | sed 's/^/    /'; correct=0
            else
                echo "  ran all $(wc -l < /base_suite_inventory.txt) tests of this step's base"
            fi
        fi
        if [ "$(cat /base_suite_rc)" = 0 ] && [ "$cand_rc" != 0 ]; then
            echo "  the suite exits $cand_rc here and passed in this step's base"; correct=0
        fi
    fi
fi

echo "== end-to-end non-regression gate =="
# The baseline is the ladder's L0 floor -- the tree with every one of this
# task's stages removed -- not this step's own base.  A no-op agent's tree IS
# L0 at every step, so a guard baselined on the step's own base would report a
# correct no-op as catastrophically slow and ask the wrong question.  Both
# sides are compiled from their own headers; only the whole-pass time matters,
# and this is a pass/fail gate, never scored.
if [ "$correct" = 1 ]; then
    ingest=$(time_guard /baseh /tmp/h_cand ingest) || ingest=""
    if [ -n "$ingest" ]; then
        read -r ingest_base ingest_cand _sb _sc <<< "$ingest"
        echo "  whole ingest pass: L0 floor ${ingest_base} us   candidate ${ingest_cand} us"
        if awk -v a="$ingest_base" -v b="$ingest_cand" 'BEGIN{exit !(b > a * 1.25)}'; then
            echo "  REGRESSED: more than 25% slower than the L0 floor (regression gate disabled)"
        fi
    else
        echo "  could not time the ingest pass"; correct=0
    fi
fi

echo "== per-function measurement =="
# Measured even when a gate has already failed, so that a failing tree reports
# the numbers that explain it rather than only a zero.  Correctness still
# multiplies, so the reward is 0 either way.
if [ -x /tmp/h_cand ]; then
    measure_functions /baseh /tmp/h_cand "$FUNCS" speedups correct
fi

if [ "${#speedups[@]}" -eq 0 ]; then
    echo "no functions measured"; report 0
fi
echo "  measured ${#speedups[@]} functions"
report "$correct" "${speedups[@]}"
