#!/usr/bin/env bash
# Verifier for step 01 -- RLP transaction input and the London fork.
#
# reward = gate * score.
#
#   gate  in {0,1}  every GATE group passes completely, or this step is zero
#   score in [0,1]  weighted pass fraction over the SCORED groups only
#
# The GATE groups are proof of life plus inherited regression: one
# deterministic end-to-end path per milestone, carried forward. They earn
# nothing -- they only veto. The SCORED groups are the capability this step
# adds plus its peripheral breadth; their weights sum to 1.0 among themselves
# and partial credit inside them is real.
#
# Which group is which is not decided in this file. It is declared in
# /tests/groups.json and applied by /tests/score.py, which computes the gate
# and the score separately and multiplies them. Per-group and per-case detail,
# including the name of any gate group that vetoed the step, goes to
# /logs/verifier/reward_details.json.
#
# The two inherited groups are fed JSON transactions, not RLP. RLP transaction
# input is itself a step-01 new capability, so a Berlin corpus carrying
# txs.rlp would be gated behind it: the previous step's binary cannot read the
# file at all and would score 0 on groups meant to measure behaviour it
# already has. Measured before this file was written -- the same fixtures fed
# as RLP scored 0/200 for that binary and 712/712 fed as JSON.
set -u
source /tests/helpers.sh
require_step 01
harden_reward_dir

CANDIDATE=/app/build/t8n
LOG=/tmp/verifier.log
: > /tmp/verifier-cases.log
exec > >(tee -a "$LOG") 2>&1

echo "=== Step 01 verifier started $(date -u +%Y-%m-%dT%H:%M:%SZ) ==="

GROUPS_NEW="rlp-tx-input london-fee-market london-basefee-opcode london-refund-cap london-ef-rejection result-schema-and-errors"
GROUPS_INHERITED="inherited-berlin-state inherited-berlin-negative"

# Emit a details file even if we bail out early, so a zero is never mistaken
# for a verifier that never ran.
DETAILS=/logs/verifier/reward_details.json
GROUPS_JSON=/tests/groups.json

# write_details <status> [<group>|<kind>|<passed>|<cases> ...]
#
# Hands the rows to /tests/score.py and echoes the reward it computed. The
# <kind> field is a console label only and is dropped here: the tier that
# actually grades a group -- GATE or SCORED, and at what weight -- is read from
# groups.json by the scorer, so that this file and the manifest can never drift
# apart into two different opinions about what a group is worth.
write_details() {
    local status="$1"; shift
    local r name rest args=()
    for r in "$@"; do
        name="${r%%|*}"; rest="${r#*|}"; rest="${rest#*|}"
        args+=(--row "$name|$rest")
    done
    python3 /tests/score.py --groups-json "$GROUPS_JSON" \
        --cases-log /tmp/verifier-cases.log --out "$DETAILS" \
        --status "$status" "${args[@]}"
}

bail() {
    echo "VERIFIER: $1"
    # Proof of life is a gate group, so this is a gate veto and has to say so.
    # A step that reports 0 without naming the case that vetoed it is
    # indistinguishable from a candidate that simply did nothing.
    python3 /tests/score.py --groups-json "$GROUPS_JSON" \
        --cases-log /tmp/verifier-cases.log --out "$DETAILS" \
        --status proof_of_life_failed --gate-fail "proof-of-life|$1" >/dev/null
    chmod 644 "$DETAILS" 2>/dev/null || true
    report 0
}

# --- build the candidate ------------------------------------------------------
give_app_to_unprivileged

# TEST_INTERFACE.md promises one build entry point: /app/build.sh, which must
# produce /app/build/t8n. The verifier runs exactly that and nothing else, so a
# candidate is free to restructure the tree as long as the script still works.
#
# Three attempts, in order of how much they disturb the candidate's tree:
#
#   1. the script as delivered, offline;
#   2. the same, after forcing the memsize replacement. The seeded tree ships
#      that stub because upstream's memsize does not link under a modern Go,
#      and a candidate who deleted or broke it would otherwise fail the build
#      for a reason that has nothing to do with the milestone;
#   3. the same, with the proxy reachable, for a candidate who added a
#      dependency of their own.  This attempt is a rescue, not a graded
#      requirement: the container has the open internet (task.toml:
#      network_mode = "public"), and no group can tell a build that needed the
#      proxy from one that did not.  What is graded is that one of the three
#      produced /app/build/t8n; TEST_INTERFACE.md says exactly that.  Offline is
#      how the image is built to make attempts 1 and 2 sufficient, not a
#      separately scored clause.
#
# Only attempt 2 edits go.mod, and only in the verifier's own copy of /app --
# the checkpoint handed to the next step was captured before this container ran.
attempt_build() {
    local proxy="$1" fix_memsize="$2" label="$3" BUILD_TIMEOUT="${4:-900}"
    echo "--- Build attempt: $label ---"
    run_unprivileged env GOPROXY="$proxy" GOFLAGS=-mod=mod GOMAXPROCS=4 \
        FIX_MEMSIZE="$fix_memsize" timeout "$BUILD_TIMEOUT" bash -c '
        cd /app || exit 1
        if [ "$FIX_MEMSIZE" = yes ]; then
            go mod edit -replace github.com/fjl/memsize=/tests/memsize-stub 2>/dev/null || true
        fi
        exec bash /app/build.sh
    ' 2>&1 | tail -40
    [ -x "$CANDIDATE" ]
}

if [ ! -x "$CANDIDATE" ]; then
    [ -f /app/build.sh ] || bail "no /app/build.sh: the build entry point is missing"
    attempt_build off no  "as delivered, offline"           900 \
        || attempt_build off yes "offline, memsize stub forced" 900 \
        || attempt_build https://proxy.golang.org,direct yes "network fallback" 600
fi
[ -x "$CANDIDATE" ] || bail "/app/build.sh did not produce /app/build/t8n"
echo "--- candidate built: $(ls -l "$CANDIDATE" | awk '{print $5" bytes"}') ---"

# Bounded like every other candidate invocation: an unbounded liveness probe
# lets a candidate binary that hangs on `--help` run the harness out of its own
# clock, which ends the run with no reward file at all instead of the gate veto
# this check exists to report. 120 s is the same budget task.toml sizes it at.
run_unprivileged timeout 120 "$CANDIDATE" t8n --help >/dev/null 2>&1 \
    || bail "candidate does not answer 'evm t8n --help'"

# --- run the groups -----------------------------------------------------------
# The Oracle runs a case in well under a second; 30 s is the ceiling, not the
# expectation. JOBS matches the verifier container's 4 CPUs.
JOBS=4
PER_CASE=30
ROWS=()
run_corpus_group() {
    local name="$1" kind="$2" fork="$3" out passed cases
    if [ ! -d "/tests/corpora/$name/cases" ]; then
        echo "VERIFIER: corpus missing for group $name"
        ROWS+=("$name|$kind|0|0")
        return
    fi
    # Two clocks. PER_CASE bounds one t8n invocation; the group clock bounds the
    # whole group at its own worst case, so a candidate whose tool hangs on
    # every case still ends with a scorecard instead of a harness kill -- and a
    # harness kill is what turns "the verifier never finished" into a
    # complete-looking partial score.
    local n cap
    n=$(ls -d "/tests/corpora/$name/cases"/*/ 2>/dev/null | wc -l)
    cap=$(( 90 + ((n + JOBS - 1) / JOBS) * PER_CASE ))
    out=$(JOBS="$JOBS" timeout "$cap" bash /tests/run_group.sh "$name" "$fork" "/tests/corpora/$name" "$CANDIDATE" "$PER_CASE" 2>>/tmp/verifier-cases.log)
    passed=${out%% *}; cases=${out##* }
    case "$passed$cases" in
        ''|*[!0-9]*) echo "VERIFIER: group $name produced no summary (group clock $cap s expired, or crash)"
                     passed=0; cases=$n ;;
    esac
    printf 'GROUP %-28s %-10s %s/%s\n' "$name" "$kind" "$passed" "$cases"
    ROWS+=("$name|$kind|$passed|$cases")
}

run_corpus_group rlp-tx-input             new       London
run_corpus_group london-fee-market        new       London
run_corpus_group london-basefee-opcode    new       London
run_corpus_group london-refund-cap        new       London
run_corpus_group london-ef-rejection      new       London
run_corpus_group inherited-berlin-state   inherited Berlin
run_corpus_group inherited-berlin-negative inherited Berlin

PROBE_CASE=$(ls -d /tests/corpora/rlp-tx-input/cases/*/ 2>/dev/null | head -1)
PROBE_CASE=${PROBE_CASE%/}
if [ -n "$PROBE_CASE" ]; then
    # Bound the schema group with the same pattern used by run_corpus_group so
    # that a candidate whose tool hangs on every case still ends with a
    # scorecard rather than a harness kill.  Seven t8n invocations, each capped
    # at 120 s by schema_probe.sh itself; an outer clock of 7*120+90=930 s is
    # the absolute ceiling.
    schema_cap=$(( 7 * 120 + 90 ))
    out=$(timeout "$schema_cap" bash /tests/schema_probe.sh "$CANDIDATE" "$PROBE_CASE" 2>>/tmp/verifier-cases.log)
    probe_rc=$?
    passed=${out%% *}; cases=${out##* }
    if [ "$probe_rc" = 124 ] || [ -z "$passed$cases" ]; then
        echo "VERIFIER: schema group clock ($schema_cap s) expired or crash"
        passed=0; cases=20
    fi
    printf 'GROUP %-28s %-10s %s/%s\n' "result-schema-and-errors" "new" "$passed" "$cases"
    ROWS+=("result-schema-and-errors|new|${passed:-0}|${cases:-0}")
else
    ROWS+=("result-schema-and-errors|new|0|0")
fi

REWARD=$(write_details complete "${ROWS[@]}")
chmod 644 "$DETAILS" 2>/dev/null || true

echo "--- per-case detail ---"
sort /tmp/verifier-cases.log | awk -F'\t' '$4=="FAIL"' | head -60
cp /tmp/verifier-cases.log /logs/verifier/cases.log 2>/dev/null || true
chmod 644 /logs/verifier/cases.log 2>/dev/null || true


python3 - "$DETAILS" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
print("--- gate (pass completely or the step is zero; earns nothing) ---")
for g in d["gate"]["groups"]:
    print("  %-30s %-12s %4s/%-4s %s" % (
        g["group"], g.get("status", "ran"), g["cases_passed"], g["cases"],
        "PASS" if g["passed"] else "VETO"))
    for c in g["failing_cases"]:
        print("        failing: %s" % c)
print("--- scored (weights sum to 1.0; partial credit is real) ---")
for g in d["scored_groups"]:
    print("  %-30s w=%-5s %4s/%-4s rate=%-9s contributes %s" % (
        g["group"], g["weight"], g["cases_passed"], g["cases"], g["rate"],
        g["contribution"]))
print("gate=%d  score=%.6f  reward=%.6f" % (d["gate"]["value"], d["score"], d["reward"]))
if not d["gate"]["passed"]:
    print(d.get("zero_reason", ""))
for n in d["notes"]:
    print("NOTE: %s" % n)
PY
echo "=== Step 01: reward $REWARD ==="
report "$REWARD"
