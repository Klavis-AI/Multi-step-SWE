#!/usr/bin/env bash
# Verifier for step 07 (LoongArch64, --defsym and non-string section merging) of `elf-linker-multiversion`.
#
# Grades the linker the candidate left in /app by building it and running a
# corpus of hermetic conformance cases against it. Every case builds its own
# relocatable objects from text shipped in this image, links them with the
# candidate's `wild`, and asserts on the ELF that came out or on what it does
# when executed. Nothing here reads the network, the host, or /app outside the
# build.
#
#   reward = gate * score
#
# GATE groups are pass/fail vetoes that earn nothing: proof of life (the
# candidate builds and the artifact starts) plus one deterministic end-to-end
# path per milestone inherited from an earlier step. If any of them fails the
# step scores exactly 0 no matter what else passed, and reward_details.json
# names the component that failed.
#
# SCORED groups are this step's own new capability. Their weights are relative
# integers in groups.tsv and are normalised here so the scored weights sum to
# 1.0; score is the weighted fraction of them that passed. A submission that
# preserves every earlier milestone and implements nothing new therefore gets
# gate=1, score=0, reward=0.
#
# Every group is still run after a failure, so the scorecard shows the whole
# picture rather than stopping at the first red.
set -u

# ---- step binding ----------------------------------------------------------
# Docker keys a build-context file by (path, size, mtime-to-the-second) and
# never by content, so nine near-identical tests/ trees written at the same time
# can get step 01's tests baked into step 09's image. This block proves at run
# time which tests actually arrived.
__hb_want="07"
__hb_got="$(cat /etc/harbor-step-id 2>/dev/null || true)"
if [ "$__hb_want" != "$__hb_got" ]; then
    mkdir -p /logs/verifier
    printf '{"reward": 0, "verifier_error": "step_mismatch", "want": "%s", "got": "%s"}\n' \
        "$__hb_want" "$__hb_got" > /logs/verifier/reward.json
    echo "FATAL: step binding mismatch - tests for step $__hb_want ran in the image for step '$__hb_got'" >&2
    exit 0
fi

. /tests/helpers.sh

STEP_ID="07"
STEP_NAME="LoongArch64, --defsym and non-string section merging"
BUILD_TIMEOUT=900
CASE_BUDGET_TOTAL=1800
PER_CASE_TIMEOUT=60
START_TIMEOUT=60

WORK=/tmp/verify
SCRATCH=$WORK/scratch
mkdir -p "$WORK" "$SCRATCH" /logs/verifier
export CARGO_TARGET_DIR=${CARGO_TARGET_DIR:-/opt/cargo-target}

reported=0
build_note=""
corpus_ok=false
build_ok=false
starts_ok=false
cases_ran=false
budget_exhausted=false
gate_failed=""
gate_reason=""
declare -A GROUP_SCORE=()
declare -A GROUP_FAIL=()

json_str() { printf '%s' "${1:-}" | tr -d '"\\' | tr '\n\t' '  ' | cut -c1-300; }

note_gate_failure() { # note_gate_failure <component> <reason>
    [ -n "$gate_failed" ] && return 0   # first failure is the one that is reported
    gate_failed=$1
    gate_reason=$2
}

write_scorecard() { # write_scorecard <gate> <score> <reward>
    local gate=$1 score=$2 reward=$3 gid kind weight title first
    {
        printf '{\n'
        printf '  "step": "%s",\n' "$STEP_ID"
        printf '  "step_name": "%s",\n' "$STEP_NAME"
        printf '  "model": "reward = gate * score",\n'
        printf '  "reward": %s,\n' "$reward"
        printf '  "gate": %s,\n' "$gate"
        printf '  "score": %s,\n' "$score"
        printf '  "gate_failed": "%s",\n' "$(json_str "$gate_failed")"
        printf '  "gate_failure_reason": "%s",\n' "$(json_str "$gate_reason")"
        printf '  "corpus_ok": %s,\n' "$corpus_ok"
        # The digest this run actually graded. corpus_ok compares the corpus
        # against a CORPUS.sha256 that travels in the same image, so the two
        # agree with each other whichever version they are; the computed value
        # identifies which corpus was graded.
        printf '  "corpus_sha": "%s",\n' "${corpus_sha:-}"
        printf '  "corpus_count": %s,\n' "${corpus_count:-0}"
        printf '  "build_ok": %s,\n' "$build_ok"
        printf '  "artifact_starts": %s,\n' "$starts_ok"
        printf '  "budget_exhausted": %s,\n' "$budget_exhausted"
        printf '  "build_note": "%s",\n' "$(json_str "$build_note")"

        # --- per-case results, the whole corpus.
        # A group's first_failure below names only its first failing case, and
        # the per-case file in this container's /tmp/verify is not collected,
        # so the full list is written here. Emitted on the early-exit paths
        # too: an empty array when the corpus never ran.
        printf '  "case_results": [\n'
        first=1
        if [ -f "${results:-}" ]; then
            while read -r cid cstatus crest; do
                [ -z "${cid:-}" ] && continue
                [ $first -eq 1 ] || printf ',\n'
                first=0
                printf '    {"case": "%s", "status": "%s", "detail": "%s"}' \
                    "$(json_str "$cid")" "$(json_str "$cstatus")" "$(json_str "$crest")"
            done < "$results"
        fi
        printf '\n  ],\n'

        # --- gate components: the synthetic proof-of-life one, then the manifest's
        printf '  "gate_groups": {\n'
        printf '    "gate-build": {"passed": %s, "reason": "%s"}' \
            "$( [ "$build_ok" = true ] && [ "$starts_ok" = true ] && echo 1 || echo 0 )" \
            "$(json_str "$build_note")"
        while IFS=$'\t' read -r gid kind weight title; do
            [ -z "${gid:-}" ] && continue
            case "$gid" in \#*) continue ;; esac
            [ "$kind" = gate ] || continue
            printf ',\n    "%s": {"passed": %s, "title": "%s", "reason": "%s"}' \
                "$gid" "${GROUP_SCORE[$gid]:-0}" "$(json_str "$title")" \
                "$(json_str "${GROUP_FAIL[$gid]:-}")"
        done < /tests/groups.tsv
        printf '\n  },\n'

        # --- scored groups
        printf '  "scored_groups": {\n'
        first=1
        while IFS=$'\t' read -r gid kind weight title; do
            [ -z "${gid:-}" ] && continue
            case "$gid" in \#*) continue ;; esac
            [ "$kind" = scored ] || continue
            [ $first -eq 1 ] || printf ',\n'
            first=0
            printf '    "%s": {"weight": %s, "weight_share": %s, "passed": %s, "title": "%s", "first_failure": "%s"}' \
                "$gid" "$weight" \
                "$(awk -v w="$weight" -v t="${SCORED_TOTAL:-0}" 'BEGIN { printf "%.6f", (t > 0 ? w / t : 0) }')" \
                "${GROUP_SCORE[$gid]:-0}" "$(json_str "$title")" \
                "$(json_str "${GROUP_FAIL[$gid]:-}")"
        done < /tests/groups.tsv
        printf '\n  }\n}\n'
    } > /logs/verifier/reward_details.json
}

fail_gate() { # fail_gate <component> <reason> -- an unrecoverable gate failure
    note_gate_failure "$1" "$2"
    build_note="${build_note}${build_note:+; }$2"
    echo "VERIFIER: GATE FAILED [$1] $2" >&2
    write_scorecard 0 0.000000 0.000000
    reported=1
    report 0
}

on_exit() {
    local rc=$?
    if [ "$reported" -eq 0 ]; then
        note_gate_failure "gate-build" "verifier exited unexpectedly rc=$rc"
        build_note="${build_note}${build_note:+; }verifier exited unexpectedly rc=$rc"
        write_scorecard 0 0.000000 0.000000
        report 0
    fi
}
trap on_exit EXIT

# SCORED_TOTAL is needed by write_scorecard even on the early-exit paths.
SCORED_TOTAL=$(awk -F'\t' '$1 !~ /^#/ && $2 == "scored" { t += $3 } END { print t + 0 }' /tests/groups.tsv)

# ---- corpus integrity ------------------------------------------------------
# A prerequisite, not a score point: if the corpus that arrived is not the
# corpus that was authored, the number this verifier would print means nothing.
if [ ! -f /tests/CORPUS.sha256 ]; then
    fail_gate "gate-corpus" "no /tests/CORPUS.sha256 - the corpus manifest did not reach the image"
fi
CORPUS_COUNT_EXPECTED=$(awk '$1=="count"{print $2}' /tests/CORPUS.sha256)
CORPUS_SHA_EXPECTED=$(awk '$1=="sha256"{print $2}' /tests/CORPUS.sha256)
corpus_count=$(find /tests/cases -type f -name '*.case' | wc -l)
corpus_sha=$( { find /tests/cases -type f -name '*.case' -printf '%P\n' | LC_ALL=C sort; \
                find /tests/cases -type f -name '*.case' -print0 | LC_ALL=C sort -z | xargs -0 cat; \
                cat /tests/lib/caselib.sh /tests/lib/run-cases.sh /tests/groups.tsv; } | sha256sum | cut -d' ' -f1)
if [ "$corpus_count" != "$CORPUS_COUNT_EXPECTED" ]; then
    fail_gate "gate-corpus" "case corpus has $corpus_count cases, expected $CORPUS_COUNT_EXPECTED"
fi
if [ "$corpus_sha" != "$CORPUS_SHA_EXPECTED" ]; then
    fail_gate "gate-corpus" "case corpus digest $corpus_sha != expected $CORPUS_SHA_EXPECTED"
fi
corpus_ok=true

if [ "$SCORED_TOTAL" -le 0 ]; then
    fail_gate "gate-corpus" "groups.tsv defines no scored groups - nothing could ever be earned"
fi

# ---- gate component 1 of 2: proof of life ----------------------------------
# The candidate builds, and the artifact it builds starts. "Starts" is
# deliberately weak: step 01's scaffold exits non-zero because it cannot link
# yet, and a proof-of-life gate that demanded a successful link would collapse
# partial credit into all-or-nothing for the whole task.
if [ ! -f /app/Cargo.toml ]; then
    fail_gate "gate-build" "no /app/Cargo.toml - nothing to build"
fi
rm -f "$CARGO_TARGET_DIR/release/wild" /app/target/release/wild 2>/dev/null || true

build_log=$WORK/build.log
( cd /app && timeout "$BUILD_TIMEOUT" cargo build --release --bin wild --jobs 4 ) >"$build_log" 2>&1
build_rc=$?
WILDBIN=""
for c in "$CARGO_TARGET_DIR/release/wild" /app/target/release/wild; do
    [ -x "$c" ] && { WILDBIN=$c; break; }
done
if [ -z "$WILDBIN" ]; then
    # A workspace whose binary target is not reachable by name still passes if a
    # plain release build produces it; try once before giving up.
    ( cd /app && timeout "$BUILD_TIMEOUT" cargo build --release --jobs 4 ) >>"$build_log" 2>&1
    build_rc=$?
    for c in "$CARGO_TARGET_DIR/release/wild" /app/target/release/wild; do
        [ -x "$c" ] && { WILDBIN=$c; break; }
    done
fi
if [ -z "$WILDBIN" ]; then
    fail_gate "gate-build" "no linker binary named 'wild' after cargo build (rc=$build_rc): $(tail -6 "$build_log" | tr '\n' ' ' | cut -c1-400)"
fi
build_ok=true
echo "VERIFIER: linker under test = $WILDBIN"

# An exit status of 128+N means a fatal signal; 124 is the timeout; 125..127
# mean the thing could not be executed at all. Anything else -- including the
# 255 the step 01 scaffold returns -- is a process that ran and made a decision.
rc_is_start() {
    case "$1" in
        124|125|126|127) return 1 ;;
        12[89]|1[3-8][0-9]|19[0-2]) return 1 ;;
        *) return 0 ;;
    esac
}
start_rc=0
timeout "$START_TIMEOUT" "$WILDBIN" --version >"$WORK/start.log" 2>&1; start_rc=$?
if ! rc_is_start "$start_rc"; then
    timeout "$START_TIMEOUT" "$WILDBIN" >>"$WORK/start.log" 2>&1; start_rc=$?
fi
if ! rc_is_start "$start_rc"; then
    fail_gate "gate-build" "the linker binary does not start: exit status $start_rc from '$WILDBIN --version' and from a bare invocation: $(tail -3 "$WORK/start.log" | tr '\n' ' ' | cut -c1-300)"
fi
starts_ok=true
build_note="built $WILDBIN; artifact starts (exit $start_rc)"

# ---- run the corpus --------------------------------------------------------
# gate-* sorts before s*-* in C order, so the gate cases run first and a case
# budget that runs out costs breadth rather than the veto path.
results=$WORK/results.txt
start=$(date +%s)
SCRATCH=$SCRATCH PER_CASE_TIMEOUT=$PER_CASE_TIMEOUT \
    timeout "$CASE_BUDGET_TOTAL" bash /tests/lib/run-cases.sh "$WILDBIN" /tests/cases > "$results" 2>>"$WORK/cases.err"
run_rc=$?
elapsed=$(( $(date +%s) - start ))
[ "$run_rc" -eq 124 ] && budget_exhausted=true
cases_ran=true
echo "VERIFIER: corpus finished in ${elapsed}s rc=$run_rc, $(wc -l < "$results") case results"

# ---- aggregate -------------------------------------------------------------
grade_group() { # grade_group <gid> -- fills GROUP_SCORE/GROUP_FAIL
    local gid=$1 ncases nfail firstfail nexpected
    # The CORPUS, not the results file, fixes how many cases a group owes.
    # Taking the count from "$results" alone makes a truncated run -- the
    # CASE_BUDGET_TOTAL timeout above is recorded in budget_exhausted but does
    # not stop grading -- able to credit a group that straddles the cut-off:
    # the cases that got as far as being emitted all passed, nfail is 0, and the
    # ones that never ran are invisible. The .case files the manifest already
    # pins are the authority, so a short group fails instead of scoring 1.
    nexpected=$(find "/tests/cases/$gid" -type f -name '*.case' 2>/dev/null | wc -l)
    ncases=$(awk -v g="$gid/" 'index($1, g) == 1 { n++ } END { print n + 0 }' "$results")
    nfail=$(awk -v g="$gid/" 'index($1, g) == 1 && $2 != "ok" { n++ } END { print n + 0 }' "$results")
    firstfail=$(awk -v g="$gid/" 'index($1, g) == 1 && $2 != "ok" { print; exit }' "$results")
    if [ "$nexpected" -eq 0 ]; then
        GROUP_SCORE[$gid]=0
        GROUP_FAIL[$gid]="groups.tsv names $gid but /tests/cases/$gid holds no .case file"
    elif [ "$ncases" -eq 0 ]; then
        GROUP_SCORE[$gid]=0
        GROUP_FAIL[$gid]="group produced no results$( [ "$budget_exhausted" = true ] && echo ' (case budget exhausted)')"
    elif [ "$ncases" -ne "$nexpected" ]; then
        GROUP_SCORE[$gid]=0
        GROUP_FAIL[$gid]="only $ncases of $nexpected cases produced a result$( [ "$budget_exhausted" = true ] && echo ' (case budget exhausted)')"
    elif [ "$nfail" -eq 0 ]; then
        GROUP_SCORE[$gid]=1
        GROUP_FAIL[$gid]=""
    else
        GROUP_SCORE[$gid]=0
        GROUP_FAIL[$gid]="$nfail/$ncases failed: $firstfail"
    fi
}

# --- gate component 2 of 2: the inherited-milestone groups
gate=1
while IFS=$'\t' read -r gid kind weight title; do
    [ -z "${gid:-}" ] && continue
    case "$gid" in \#*) continue ;; esac
    [ "$kind" = gate ] || continue
    grade_group "$gid"
    if [ "${GROUP_SCORE[$gid]}" != 1 ]; then
        gate=0
        note_gate_failure "$gid" "$title -- ${GROUP_FAIL[$gid]}"
    fi
    printf 'GATE  %-34s %s\n' "$gid" \
        "$([ "${GROUP_SCORE[$gid]}" = 1 ] && echo PASS || echo "FAIL ${GROUP_FAIL[$gid]}")"
done < /tests/groups.tsv

# --- the scored groups
earned=0
while IFS=$'\t' read -r gid kind weight title; do
    [ -z "${gid:-}" ] && continue
    case "$gid" in \#*) continue ;; esac
    [ "$kind" = scored ] || continue
    grade_group "$gid"
    [ "${GROUP_SCORE[$gid]}" = 1 ] && earned=$(( earned + weight ))
    printf 'GROUP %-34s w=%-3s %s\n' "$gid" "$weight" \
        "$([ "${GROUP_SCORE[$gid]}" = 1 ] && echo PASS || echo "FAIL ${GROUP_FAIL[$gid]}")"
done < /tests/groups.tsv

score=$(awk -v e="$earned" -v t="$SCORED_TOTAL" 'BEGIN { printf "%.6f", e / t }')
reward=$(awk -v g="$gate" -v s="$score" 'BEGIN { printf "%.6f", g * s }')

if [ "$gate" -eq 1 ]; then
    echo "VERIFIER: gate PASSED; scored $earned of $SCORED_TOTAL weight -> score $score"
else
    echo "VERIFIER: gate FAILED [$gate_failed] $gate_reason" >&2
    echo "VERIFIER: (the scored side would have been $score, but the gate is a veto)"
fi
echo "VERIFIER: reward = gate($gate) * score($score) = $reward"
write_scorecard "$gate" "$score" "$reward"
reported=1
report "$reward"
