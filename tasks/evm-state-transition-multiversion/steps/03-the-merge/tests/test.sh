#!/usr/bin/env bash
# Verifier for step 03 -- the Merge (Paris).
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
# ---------------------------------------------------------------------------
# The six new groups
#
#   merge-prevrandao-opcode      EIP-4399: 0x44 stops being DIFFICULTY and
#                                returns the env's `currentRandom`. Synthetic;
#                                seven cases covering the plain reading, the
#                                full 32-byte width, a zero randao, an omitted
#                                `currentDifficulty`, the unchanged gas cost,
#                                and CALL and STATICCALL frames.
#   merge-pos-block-production   EIP-3675: difficulty 0, no block reward and no
#                                ommer reward. Synthetic; five cases.
#   merge-state-tests            150 conformance state tests at
#                                `--state.fork=Merge`, fed as RLP.
#   merge-blockchain-tests       60 block-level conformance cases at the same
#                                fork.
#   merge-result-schema          `--output.result` at the new fork: which
#                                fields it carries, that `currentDifficulty` is
#                                among them and is zero, and that a rejected
#                                transaction is still a result. Probe-driven.
#   merge-env-errors             which post-merge `--input.env` documents are
#                                well-formed. Probe-driven.
#
# Measured against the step-02 Oracle (`evm-v1.10.13`) before this file was
# written: 0/7, 0/5, 0/150, 0/60, 0/10 and 1/17. That binary does not accept
# `--state.fork=Merge` at all -- `failed constructing chain configuration:
# unsupported fork "Merge"`, exit 3 -- so a no-op continuation scores zero on
# all six by construction rather than by sampling luck. The one probe it does
# pass, `errors-london-env-unaffected`, is a deliberate pre-merge regression
# guard and its group still scores 0.
#
# Because a tool without the fork refuses *every* Merge input, the two probe
# groups would otherwise credit their refusal checks to a candidate who
# implemented nothing. Each opens with a positive control and records the
# dependent checks as failures when the control does not fire. See the header
# of merge_probe.sh.
#
# ---------------------------------------------------------------------------
# The two inherited groups
#
# London and Berlin, fed as RLP with `--declare-checks stateRoot,logsHash,
# rejected` so they assert the earlier forks' semantics and not this step's new
# result shape, plus a tail of cases that exercise the two subcommands step 02
# added and two cases that pin the earlier forks' opcode semantics. Measured
# here: the step-02 Oracle passes 159/159 on London and 156/156 on Berlin, so
# the no-op continuation keeps the gate open. Its reward is 0, not a fraction
# of it: the gate earns nothing, and the score is 0 across the scored groups,
# which a binary without this fork's capability fails entirely.
#
# The tail, and why it lives here rather than in a group of its own:
#
#   `t9n-*` / `b11r-*` (7 in London, 6 in Berlin)
#                             step 02's headline subcommands. Harbor never
#                             re-runs an earlier step's verifier, so without
#                             them a candidate could drop either subcommand at
#                             any later step and never be charged for it. Both
#                             inherited groups are GATE groups, so the way to
#                             carry an inherited obligation is to add cases to
#                             an inherited group, not to add a ninth.
#   `guard-*` (2 in London)   opcode-level regression guards for the forks this
#                             step must not disturb: one executes `0x44` and
#                             charges a tool that returns the post-merge
#                             `currentRandom` at a pre-merge fork, the other
#                             executes `0x48` and charges a tool whose opcode
#                             table stopped at Berlin. Both are ordinary `t8n`
#                             cases graded by `expect.storage`.
#
# Every case in the tail passes on the step-02 Oracle (`evm-v1.10.13`). That is
# a hard requirement, and a sharper one under `reward = gate x score`: the
# no-op continuation is seeded from the step-02 workspace and both inherited
# groups are GATE groups, so a case the step-02 Oracle fails would veto the
# gate for every candidate -- including this step's own Oracle.
#
# ---------------------------------------------------------------------------
# A note on the fork's spelling, for whoever reads this next
#
# go-ethereum v1.10.23 -- the release this step's Oracle is built from -- spells
# this fork `Merged` in its `tests` fork table, and upstream renamed it to
# `Merge` one release line later (v1.11.5). `TEST_INTERFACE.md` fixes the
# graded vocabulary at `Merge`, so the Oracle carries a one-name patch
# (`oracle/step-03-forkname.py`) that duplicates the entry under the documented
# name, leaving both working. Everything graded here passes `Merge`.
set -u
source /tests/helpers.sh
require_step 03
harden_reward_dir

CANDIDATE=/app/build/t8n
LOG=/tmp/verifier.log
: > /tmp/verifier-cases.log
exec > >(tee -a "$LOG") 2>&1

echo "=== Step 03 verifier started $(date -u +%Y-%m-%dT%H:%M:%SZ) ==="

GROUPS_NEW="merge-prevrandao-opcode merge-pos-block-production merge-state-tests merge-blockchain-tests merge-result-schema merge-env-errors"
GROUPS_INHERITED="inherited-london-state inherited-berlin-state"

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
#      dependency of their own.
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

# Both probe groups are direct invocations rather than corpora: one grades the
# result document's shape at the new fork, the other which post-merge env
# documents are well-formed. Neither is expressible as a state-test fixture.
run_probe_group() {
    # <want> is the number of cases merge_probe.sh is written to emit for this
    # group. It is only used when the probe produces no summary at all: pinning
    # the denominator keeps "produced no verdicts" from reading as "this group
    # does not exist".
    local name="$1" which="$2" want="$3" out passed cases
    if [ ! -f /tests/probe/alloc.json ] || [ ! -f /tests/merge_probe.sh ]; then
        echo "VERIFIER: probe inputs are missing for group $name"
        ROWS+=("$name|new|0|$want")
        return
    fi
    out=$(timeout 600 bash /tests/merge_probe.sh "$CANDIDATE" /tests/probe "$which" \
              2>>/tmp/verifier-cases.log)
    passed=${out%% *}; cases=${out##* }
    case "$passed$cases" in
        ''|*[!0-9]*)
        echo "VERIFIER: group $name produced no summary (its 600 s clock expired, or it crashed)"
        passed=0; cases=$want ;;
    esac
    printf 'GROUP %-28s %-10s %s/%s\n' "$name" "new" "$passed" "$cases"
    ROWS+=("$name|new|${passed:-0}|${cases:-0}")
}

run_corpus_group merge-prevrandao-opcode     new       Merge
run_corpus_group merge-pos-block-production  new       Merge
run_corpus_group merge-state-tests           new       Merge
run_corpus_group merge-blockchain-tests      new       Merge
run_probe_group  merge-result-schema         result-schema 10
run_probe_group  merge-env-errors            env-errors    17
run_corpus_group inherited-london-state      inherited London
run_corpus_group inherited-berlin-state      inherited Berlin

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
echo "=== Step 03: reward $REWARD ==="
report "$REWARD"
