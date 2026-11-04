#!/usr/bin/env bash
# Verifier for step 15 of turso-engine-multiversion.
#
# Grading crosses the process boundary only. This script never compiles,
# imports or type-checks Rust: it locates the shell binary the candidate
# installed, runs it as a subprocess, and reads what it printed and what it
# wrote to disk. A candidate whose crate layout diverges entirely still scores
# full marks if its answers and its files are right.

set -uo pipefail

TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ASSETS="$TESTS_DIR/assets"
LOGDIR=/logs/verifier
mkdir -p "$LOGDIR"

. "$TESTS_DIR/helpers.sh"

# ---------------------------------------------------------------- step binding
# The Docker build context is keyed by (relative path, size, mtime-to-the-
# second) and never by content, so two steps whose tests differ only in small
# ways can silently share a cached layer. /etc/harbor-step-id is baked into the
# image before the first COPY; if it disagrees with the step this script was
# written for, the image is the wrong one and no score it produces is real.
require_step() {
    local want="$1" got=""
    [ -r /etc/harbor-step-id ] && got="$(tr -d '[:space:]' < /etc/harbor-step-id)"
    if [ "$got" != "$want" ]; then
        {
            echo "step binding mismatch"
            echo "  test.sh belongs to step: $want"
            echo "  image /etc/harbor-step-id: ${got:-<absent>}"
        } > "$LOGDIR/step-mismatch.log"
        mkdir -p "$LOGDIR"
        printf '{"reward": 0, "verifier_error": "step_mismatch", "expected_step": "%s", "image_step": "%s"}\n' \
            "$want" "${got:-absent}" > "$LOGDIR/reward_details.json"
        report 0
    fi
}
require_step "15"

# ------------------------------------------------------------------ bookkeeping
SCRATCH="$(mktemp -d /tmp/verify.XXXXXXXX)"
export SCRATCH
# Every group is exactly one KIND, and the kind decides how it scores:
#
#   gate        proof of life + one end-to-end path per inherited milestone.
#               Binary as a set. Earns nothing. Vetoes the whole step.
#   new         this step's new capability -- the bulk of the scored weight.
#   peripheral  breadth at or before this step -- the remainder.
#
# reward = gate x score, where gate is 1 only if EVERY gate group passed
# completely, and score is the weighted pass fraction over the scored groups
# alone. A no-op therefore scores exactly 0: it preserves the inherited
# behaviour so the gate passes, and implements nothing new so the score is 0.
GROUP_NAMES=(); GROUP_PASSED=(); GROUP_TOTAL=(); GROUP_KIND=(); GROUP_WEIGHT=(); GROUP_NOTE=()
GATE_EXPECTED=0
# Sum of the weights of every scored group groups.txt declares, set once the
# assets are readable. It is the denominator of the score, so a group the
# verifier never reached counts as zero rather than vanishing from the
# division. 0 means "not yet known"; compute_score then falls back to the
# recorded sum, which is all it can do before the file has been read.
SCORED_WEIGHT_DECLARED=0
FINISHED=0

cleanup() { rm -rf "$SCRATCH"; }

# A gate failure is all-or-nothing by design, so a step that reports 0 must say
# which gate group vetoed it and why. Without that, a vetoed step is
# indistinguishable from a candidate that did nothing -- and those two need
# completely different responses from whoever reads the result.
write_details() {
    local reward="$1" status="$2" note="$3"
    local i first=1
    {
        printf '{\n'
        printf '  "step": "15",\n'
        printf '  "status": "%s",\n' "$status"
        printf '  "note": %s,\n' "$(json_str "$note")"
        printf '  "reward": %s,\n' "$reward"
        printf '  "gate": {"passed": %s, "failed_groups": [%s], "note": %s},\n' \
            "$(gate_passed)" "$(gate_failed_json)" "$(json_str "$(gate_note)")"
        printf '  "score": %s,\n' "$(compute_score)"
        printf '  "groups": [\n'
        for i in "${!GROUP_NAMES[@]}"; do
            [ $first = 1 ] || printf ',\n'
            first=0
            printf '    {"name": "%s", "kind": "%s", "weight": %s, "passed": %s, "total": %s, "note": %s}' \
                "${GROUP_NAMES[$i]}" "${GROUP_KIND[$i]}" "${GROUP_WEIGHT[$i]}" \
                "${GROUP_PASSED[$i]}" "${GROUP_TOTAL[$i]}" "$(json_str "${GROUP_NOTE[$i]}")"
        done
        printf '\n  ]\n}\n'
    } > "$LOGDIR/reward_details.json"
}

json_str() {
    printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/\t/ /g' \
        | awk 'BEGIN{printf "\""} {printf "%s", $0} END{printf "\""}'
}

# Anything that reaches the exit trap without finish() having run is a verifier
# defect, not a candidate failure -- but the candidate must still be scored, so
# it is scored on whatever groups completed rather than crashing the trial.
on_exit() {
    local rc=$?
    if [ "$FINISHED" != 1 ]; then
        local partial; partial="$(compute_reward)"
        write_details "$partial" "verifier_aborted" \
            "verifier exited unexpectedly (rc=$rc) after ${#GROUP_NAMES[@]} groups"
        cleanup
        report "$partial"
    fi
    cleanup
}
trap on_exit EXIT

# gate: 1 only when every group of kind "gate" passed every one of its cases.
# A gate group with no cases recorded (the verifier aborted before reaching it)
# counts as failed -- an unmeasured gate must not open.
gate_passed() {
    local i
    for i in "${!GROUP_NAMES[@]}"; do
        [ "${GROUP_KIND[$i]}" = gate ] || continue
        if [ "${GROUP_PASSED[$i]}" != "${GROUP_TOTAL[$i]}" ] || [ "${GROUP_TOTAL[$i]}" -le 0 ]; then
            printf 'false'; return
        fi
    done
    if [ "$GATE_EXPECTED" -gt 0 ] && [ "$(gate_seen)" -lt "$GATE_EXPECTED" ]; then
        printf 'false'; return
    fi
    printf 'true'
}
gate_seen() {
    local i n=0
    for i in "${!GROUP_NAMES[@]}"; do [ "${GROUP_KIND[$i]}" = gate ] && n=$((n + 1)); done
    printf '%s' "$n"
}
gate_failed_json() {
    local i first=1
    for i in "${!GROUP_NAMES[@]}"; do
        [ "${GROUP_KIND[$i]}" = gate ] || continue
        [ "${GROUP_PASSED[$i]}" = "${GROUP_TOTAL[$i]}" ] && [ "${GROUP_TOTAL[$i]}" -gt 0 ] && continue
        [ $first = 1 ] || printf ', '
        first=0
        printf '"%s"' "${GROUP_NAMES[$i]}"
    done
}
# One sentence naming the vetoing gate group(s) and the first failing case in each.
gate_note() {
    local i out=""
    for i in "${!GROUP_NAMES[@]}"; do
        [ "${GROUP_KIND[$i]}" = gate ] || continue
        [ "${GROUP_PASSED[$i]}" = "${GROUP_TOTAL[$i]}" ] && [ "${GROUP_TOTAL[$i]}" -gt 0 ] && continue
        out="${out}${out:+; }${GROUP_NAMES[$i]} ${GROUP_PASSED[$i]}/${GROUP_TOTAL[$i]}: ${GROUP_NOTE[$i]}"
    done
    if [ -z "$out" ] && [ "$GATE_EXPECTED" -gt 0 ] && [ "$(gate_seen)" -lt "$GATE_EXPECTED" ]; then
        out="only $(gate_seen) of $GATE_EXPECTED gate groups were recorded"
    fi
    [ -n "$out" ] && printf 'gate vetoed: %s' "$out" || printf 'all gate groups passed'
}

# score: weighted pass fraction over the scored groups alone. Gate groups do
# not appear in either the numerator or the denominator -- they earn nothing.
#
# The denominator is the weight groups.txt DECLARES, not the weight actually
# recorded. The two are equal on every completed run. They differ only when the
# verifier aborts part way through, and there the difference is the whole point:
# dividing by the recorded subset reports the pass fraction of the groups that
# happened to run, so an abort after one passing group reports 1.000 on two per
# cent of the corpus. Dividing by the declared total scores an unreached group
# as zero, which is what an unmeasured group is worth. This can only lower an
# aborted score; it cannot change a completed one, and it cannot fail a correct
# solution.
compute_score() {
    local i num=0 den=0
    for i in "${!GROUP_NAMES[@]}"; do
        [ "${GROUP_KIND[$i]}" = gate ] && continue
        num=$(awk -v n="$num" -v w="${GROUP_WEIGHT[$i]}" -v p="${GROUP_PASSED[$i]}" \
                  -v t="${GROUP_TOTAL[$i]}" 'BEGIN{ if (t<=0) t=1; printf "%.10f", n + w*(p/t) }')
        den=$(awk -v d="$den" -v w="${GROUP_WEIGHT[$i]}" 'BEGIN{printf "%.10f", d + w}')
    done
    den=$(awk -v r="$den" -v d="$SCORED_WEIGHT_DECLARED" \
              'BEGIN{ printf "%.10f", (d > 0 ? d : r) }')
    awk -v n="$num" -v d="$den" 'BEGIN{ if (d<=0) { print "0"; } else { printf "%.6f", n/d } }'
}

# reward = gate x score.
compute_reward() {
    if [ "$(gate_passed)" = true ]; then compute_score; else printf '0'; fi
}

record() {
    GROUP_NAMES+=("$1"); GROUP_KIND+=("$2"); GROUP_WEIGHT+=("$3")
    GROUP_PASSED+=("$4"); GROUP_TOTAL+=("$5"); GROUP_NOTE+=("$6")
    printf 'GROUP %-34s %-10s %5s  %3s/%-3s  %s\n' "$1" "$2" "$3" "$4" "$5" "$6"
}

finish() {
    local reward; reward="$(compute_reward)"
    FINISHED=1
    write_details "$reward" "ok" "$1"
    printf 'GATE %s  SCORE %s  REWARD %s\n' "$(gate_passed)" "$(compute_score)" "$reward"
    printf '%s\n' "$(gate_note)"
    cleanup
    report "$reward"
}

# Every declared group is recorded as 0/total with a shared diagnostic. Used
# when a prerequisite fails, so reward_details.json always lists every group
# and a reader can tell "did not run" from "ran and failed".
fail_everything() {
    local why="$1" line name count kind weight
    # A prerequisite failure is a proof-of-life failure: the gate is closed and
    # the details name the reason, whatever groups.txt declares.
    record "gate-proof-of-life" gate 0 0 1 "$why"
    while IFS=$'\t' read -r name count kind weight; do
        case "$name" in ''|'#'*) continue ;; esac
        record "$name" "$kind" "$weight" 0 "$count" "$why"
    done < "$ASSETS/groups.txt"
    local reward; reward="$(compute_reward)"
    FINISHED=1
    write_details "$reward" "prerequisite_failed" "$why"
    printf 'GATE false  REWARD 0\n%s\n' "$(gate_note)"
    cleanup
    report "$reward"
}

# ------------------------------------------------- fixture integrity (not scored)
# A corpus that has been altered cannot grade anything, so this is a
# prerequisite rather than a score point of its own.
cd "$ASSETS" || fail_everything "verifier assets missing"
if ! sha256sum -c --quiet CASE_SHA256SUMS > "$LOGDIR/asset-check.log" 2>&1; then
    fail_everything "verifier corpus failed its SHA-256 manifest"
fi
declared_cases="$(tr -d '[:space:]' < CASE_COUNT)"
actual_cases="$(find "$ASSETS/cases" -type f \( -name '*.sql' -o -name '*.sh' \) | wc -l | tr -d ' ')"
if [ "$declared_cases" != "$actual_cases" ]; then
    fail_everything "verifier corpus inventory mismatch: declared $declared_cases, found $actual_cases"
fi

# ------------------------------------------------------- locate candidate binary
# The contract fixes one path for this step's shell: /app/bin/tursodb. /app is
# untrusted candidate data, so the path is checked for symlink escape before it
# is executed.
#
# ALT_NAME below is the name the shell carried at the previous step, and is
# empty at every step except the one that renames it. The gate asks a single
# question -- does the behaviour this step inherited still work -- and at the
# renaming step that behaviour lives under the old name until the candidate
# performs the move. Pinning the gate to the new path would make a candidate
# who changed nothing fail the *regression* gate for not having migrated,
# which would put this step's own migration inside its own gate. The rename is
# new capability, so it is scored, not gated. Scored cases are always run
# against the contract path below, so the fallback cannot hand a candidate
# credit for skipping the rename -- it only keeps the veto honest.
usable_binary() {   # 0 if the path can be executed as the engine shell
    [ -n "$1" ] && [ ! -L "$1" ] && [ -f "$1" ] && [ -x "$1" ]
}
GRADED_CLI="/app/bin/tursodb"
ALT_NAME=""
ALT_CLI=""
if [ -n "$ALT_NAME" ]; then
    ALT_CLI="/app/bin/$ALT_NAME"
fi
if [ -L "$GRADED_CLI" ]; then
    fail_everything "tursodb at /app/bin/tursodb is a symlink; the contract requires a regular file"
fi
if usable_binary "$GRADED_CLI"; then
    GATE_CLI="$GRADED_CLI"
elif usable_binary "$ALT_CLI"; then
    GATE_CLI="$ALT_CLI"
elif [ -f "$GRADED_CLI" ] && [ ! -x "$GRADED_CLI" ]; then
    fail_everything "/app/bin/tursodb is not executable"
else
    fail_everything "no engine shell binary at /app/bin/tursodb"
fi
# The probe and the gate groups run against whichever shell is present.
export CLI="$GATE_CLI"
export SQLITE3="$ASSETS/bin/sqlite3"
export CASE_TIMEOUT=120
# The invocation this step's interface contract documents, and -- at the step
# where that invocation changed -- the one it superseded (empty elsewhere).
# Scored cases are always run under GRADED_MODE; see the probe below for why
# the gate may fall back.
GRADED_MODE="list"
ALT_MODE=""
export CLI_MODE="$GRADED_MODE"

. "$ASSETS/lib/runner.sh"

# How many gate groups this step declares (plus the probe). If the verifier
# aborts before recording all of them, the gate is treated as failed: an
# unmeasured gate must not open.
GATE_EXPECTED=$(( $(awk -F'\t' '$1 !~ /^(#|$)/ && $3 == "gate"' "$ASSETS/groups.txt" | wc -l) + 1 ))
SCORED_WEIGHT_DECLARED=$(awk -F'\t' '$1 !~ /^(#|$)/ && $3 != "gate" { s += $4 } END { printf "%.10f", s + 0 }' "$ASSETS/groups.txt")

# Proof of life: the binary must start and answer one trivial query. Without
# this every group would report the same failure and the log would not say why.
#
# Judge the probe by the same rule the graded cases are judged by. Blank
# separator lines are not part of the contract (see runner_normalize), and some
# releases along the chain emit one before every result block. A probe that
# compared raw stdout would reject an engine whose graded answers are all
# correct -- the gate would contradict the grading it exists to guard.
PROBE_RC=
probe_shell() {   # probe_shell <output-mode> -> 0 if the shell answers SELECT 1
    local mode="$1" saved="$CLI_MODE"
    # `local d="$SCRATCH/probe-$mode"` on the line above would expand $mode
    # before `local` assigned it: bash expands every word of the builtin's
    # argument list first.
    local d="$SCRATCH/probe-$mode"
    mkdir -p "$d"
    printf 'SELECT 1;\n' > "$d/p.sql"
    CLI_MODE="$mode"
    runner_run "$d/p.sql" "$d/out"
    CLI_MODE="$saved"
    runner_normalize sql "$d/out/stdout" "$d/out/stdout.norm"
    PROBE_RC="$(cat "$d/out/rc")"
    [ "$PROBE_RC" = 0 ] && [ "$(cat "$d/out/stdout.norm")" = "1" ]
}

# The mode the gate groups run under. It is this step's mode unless the shell
# predates the step that renamed the --output-mode values, in which case the
# gate falls back to the superseded spelling for the same reason it falls back
# to the superseded binary name: the rename is this step's new capability, and
# a gate that demanded it would veto a candidate for not having done this
# step's own work instead of scoring that work at zero. Every scored case still
# runs under GRADED_MODE, so the fallback buys no credit.
GATE_MODE="$GRADED_MODE"
probe_note="engine shell at $GATE_CLI answered 'SELECT 1;'"
if ! probe_shell "$GRADED_MODE"; then
    if [ -n "$ALT_MODE" ] && probe_shell "$ALT_MODE"; then
        GATE_MODE="$ALT_MODE"
        probe_note="engine shell at $GATE_CLI answered 'SELECT 1;' under the superseded -m $ALT_MODE"
    else
        fail_everything "engine shell did not answer 'SELECT 1;' (exit $PROBE_RC)"
    fi
fi
# The probe is the first gate group, so reward_details.json always names it.
record "gate-proof-of-life" gate 0 1 1 "$probe_note"

# ------------------------------------------------------------------ score groups
# Every group runs, whatever earlier groups did.
run_group() {
    local name="$1" declared="$2" kind="$3" weight="$4"
    local dir="$ASSETS/cases/$name"
    local executed=0 passed=0 failed_names="" c diag

    # Gate groups are judged against the shell that is present, under the mode
    # it understands (see above); scored groups are always judged against the
    # path and the invocation this step's contract names, so a scored case can
    # never be satisfied by the superseded binary or the superseded flag.
    if [ "$kind" = gate ]; then
        export CLI="$GATE_CLI" CLI_MODE="$GATE_MODE"
    else
        export CLI="$GRADED_CLI" CLI_MODE="$GRADED_MODE"
    fi

    if [ ! -d "$dir" ]; then
        record "$name" "$kind" "$weight" 0 "$declared" "group directory absent from corpus"
        return
    fi

    while IFS= read -r c; do
        executed=$((executed + 1))
        if diag="$(runner_check "$c" "$SCRATCH/run/$name/$(basename "$c")" 2>&1)"; then
            passed=$((passed + 1))
        else
            [ "$(printf '%s' "$failed_names" | wc -c)" -lt 200 ] && \
                failed_names="$failed_names $(basename "${c%.*}"):${diag}"
        fi
    done < <(find "$dir" -type f \( -name '*.sql' -o -name '*.sh' \) | LC_ALL=C sort)

    # An executed count that does not match the frozen inventory means the
    # group silently graded a different set than it was scored against.
    if [ "$executed" != "$declared" ]; then
        record "$name" "$kind" "$weight" 0 "$declared" \
            "executed-count mismatch: ran $executed of $declared declared cases"
        return
    fi

    if [ "$passed" = "$declared" ]; then
        record "$name" "$kind" "$weight" "$passed" "$declared" "all cases passed"
    else
        record "$name" "$kind" "$weight" "$passed" "$declared" \
            "failed:${failed_names}"
    fi
}

while IFS=$'\t' read -r name count kind weight; do
    case "$name" in ''|'#'*) continue ;; esac
    run_group "$name" "$count" "$kind" "$weight"
done < "$ASSETS/groups.txt"

finish "scored ${#GROUP_NAMES[@]} groups over $actual_cases cases"
