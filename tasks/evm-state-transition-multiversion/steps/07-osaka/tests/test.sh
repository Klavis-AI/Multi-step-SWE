#!/usr/bin/env bash
# Verifier for step 07 -- the Osaka fork.
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
# /logs/verifier/reward_details.json and /logs/verifier/cases.log.
#
# The six new groups are four fixture corpora, one per Osaka EIP that the
# vendored fixture trees reach -- EIP-7825 the per-transaction gas cap,
# EIP-7883 the ModExp repricing, EIP-7939 CLZ, EIP-7951 P256VERIFY -- plus two
# groups built by hand:
#
#   * `osaka-spec-vectors`, a synthetic corpus whose every expected value is
#     read out of the vendored EIP text rather than back off a tool: the six
#     CLZ vectors EIP-7939 publishes, the 5 gas CLZ price, the 500 gas ModExp
#     minimum, the 6900 gas P256VERIFY price and its 32-byte output, and the
#     2**24 gas-cap boundary. See mkspecvectors.py's header in the authoring
#     scratch for the derivation of each one.
#   * `osaka-negative`, a probe for the rejection side of EIP-7825, which no
#     state-test corpus can show: that the rejection is fork-gated, that it
#     covers every transaction type, that it removes one transaction and not
#     the block, and that it is reported in `rejected[]` with exit 0 rather
#     than by failing the process. See negative_probe.sh's header.
#
# The four fixture corpora are filtered twice, not sampled. A case is kept only
# if BOTH of these hold:
#
#   1. the step-06 Oracle (`evm-v1.15.0`) gets it wrong at `--state.fork=Osaka`;
#   2. the step-07 Oracle gets it wrong when the same case is run at
#      `--state.fork=Prague`.
#
# The second filter is the one that matters. `evm-v1.15.0` does accept the fork
# name `Osaka`, but on any Osaka input carrying `currentExcessBlobGas` it dies
# with `panic: calculating blob fee on unsupported fork` -- its CalcBlobFee has
# no Osaka case -- so filter 1 alone would keep cases that are merely
# crash-gated. Filter 2 keeps only cases whose answer genuinely differs between
# Prague rules and Osaka rules, so each group measures the milestone. Measured
# against `evm-v1.15.0` at Osaka: 0/29, 0/80, 0/80, 0/80 cases, 0/12 on the synthetic
# corpus and 0/6 on the probe -- six scored groups, all six scoring 0, since
# a group scores only when every case in it passes.
#
# Three cases in `osaka-tx-gas-cap-7825` are kept as *controls*, not as
# discriminators, and are the exception to filter 2. Each carries a transaction
# whose `gasLimit` is exactly 2**24 -- at the cap, so EIP-7825 does not reject
# it -- and an intrinsic cost just above that same number: 16,778,100 (`0016`),
# 16,777,600 (`0019`) and 16,789,800 (`0022`, whose 612 authorization entries
# cost 25,000 each). The *inherited* above-the-gas-limit rule is what rejects
# all three, at Prague exactly as at Osaka. They stay because they are correct
# expectations that a tool which implements the cap wrongly must still meet,
# and they are 3 of the group's 29 cases -- the other 26 discriminate.
#
# The last two denominators are smaller than the corpora they name, and that is
# deliberate. `osaka-spec-vectors` holds 15 case directories and the probe emits
# 13. The cases the predecessor passes are its positive controls -- regression
# guards a correct Osaka tool must also pass, not new capability -- and left inside a
# scored group they paid a no-op free credit. So 10 of them were reassigned into the
# `inherited-cli-contract`, `candidate-liveness` and `predecessor-fork-unchanged` GATE
# groups, where they still must pass but earn nothing, and `0012-p256-invalid-6900`
# was deleted outright because both pinned binaries produce a byte-identical graded
# result for it. The guarantee that a tool which refuses everything cannot score a
# group is now carried by the gate, not by padding a scored group, which is why the
# scored denominators here are 12 and 6. `groups.json` names every reassigned
# case and the group it came from.
#
#
# The two inherited groups are Prague and Cancun, fed as RLP. Each case states
# its own `expect.checks` list -- `stateRoot`, `logsHash`, `rejected` -- so they
# assert those forks' semantics and not this step's new output shape. Each also
# carries four cases that drive `t9n` and `b11r` rather than `t8n`, inherited
# from step 02: without them this step's gate cannot notice a candidate that
# deletes a subcommand while adding Osaka, because nothing after step 02
# invokes either one. Measured: `evm-v1.15.0` passes 154/154 on each, as does
# `evm-v1.16.2`.
#
# EIP-7918 (the blob base-fee floor) has NO group. It is not reachable through
# the graded interface at this Oracle: `t8n` builds the parent header for
# `CalcExcessBlobGas` with only Time, ExcessBlobGas and BlobGasUsed, while the
# EIP-7918 branch dereferences `parent.BaseFee`, so any input that reaches the
# new rule makes the Oracle itself panic on a nil pointer. There are also no
# EIP-7918 fixtures in any vendored tree. Asserting it would mean asserting
# behaviour the reference implementation does not have; the sixth group is the
# spec-vector corpus instead. EIP-7918 is therefore out of scope for this step
# and is excluded from the scored surface, not deferred.
set -u
source /tests/helpers.sh
require_step 07
harden_reward_dir

CANDIDATE=/app/build/t8n
LOG=/tmp/verifier.log
: > /tmp/verifier-cases.log
exec > >(tee -a "$LOG") 2>&1

echo "=== Step 07 verifier started $(date -u +%Y-%m-%dT%H:%M:%SZ) ==="

GROUPS_NEW="osaka-tx-gas-cap-7825 osaka-modexp-7883 osaka-clz-7939 osaka-p256-7951 osaka-spec-vectors osaka-negative"
GROUPS_INHERITED="inherited-prague-t8n inherited-cancun-t8n"

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
#      for a reason that has nothing to do with the milestone. v1.16.2 has no
#      memsize dependency at all, so on a tree that has moved that far the
#      `go mod edit` is a no-op rather than a hazard.
#   3. the same, with the proxy reachable, for a candidate who added a
#      dependency of their own.
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
# this check exists to report.
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

run_corpus_group osaka-tx-gas-cap-7825  new       Osaka
run_corpus_group osaka-modexp-7883      new       Osaka
run_corpus_group osaka-clz-7939         new       Osaka
run_corpus_group osaka-p256-7951        new       Osaka
run_corpus_group osaka-spec-vectors     new       Osaka
run_corpus_group inherited-prague-t8n   inherited Prague
run_corpus_group inherited-cancun-t8n   inherited Cancun

# The rejection side of EIP-7825 needs two forks of the same input and a look
# at the process exit status, neither of which a corpus case can express, so
# this group is driven by direct invocations. See negative_probe.sh's header.
if [ -f /tests/probe/alloc.json ]; then
    out=$(timeout 600 bash /tests/negative_probe.sh "$CANDIDATE" /tests/probe \
              2>>/tmp/verifier-cases.log)
    passed=${out%% *}; cases=${out##* }
    case "$passed$cases" in
        ''|*[!0-9]*)
        # A probe group that expires or crashes reports nothing, so pin the
        # denominator to the case count the script is written to emit. Left
        # at 0, the details file would say 0/0 -- which reads as "this group
        # does not exist" rather than "this group produced no verdicts".
        echo "VERIFIER: osaka-negative produced no summary (its 600 s clock expired, or it crashed)"
        passed=0; cases=13 ;;
    esac
    printf 'GROUP %-28s %-10s %s/%s\n' "osaka-negative" "new" "$passed" "$cases"
    ROWS+=("osaka-negative|new|${passed:-0}|${cases:-0}")
else
    echo "VERIFIER: negative probe inputs are missing"
    ROWS+=("osaka-negative|new|0|0")
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
echo "=== Step 07: reward $REWARD ==="
report "$REWARD"
