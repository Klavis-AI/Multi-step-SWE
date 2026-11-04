#!/usr/bin/env bash
# Verifier for step 02 -- the `t9n` and `b11r` subcommands.
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
# The two inherited groups are t8n corpora fed through run_group.sh's own form
# choice: `txs.rlp` where the case has one, and the case's `txs.json` where it
# has that instead -- 58 of the 300 cases. RLP transaction input
# was step 01's new capability and the previous step's Oracle has it, so
# unlike step 01 there is nothing here that gates an inherited group behind a
# capability its binary lacks -- measured before this file was written: the
# step-01 Oracle binary passes 150/150 on each of the two inherited corpora.
#
# The six new groups are gated the other way, and by construction rather than
# by hope: the step-01 Oracle has no `t9n` and no `b11r` subcommand at all
# (`No help topic for 't9n'`, exit 3), so every one of them scores zero on a
# no-op continuation. Measured: 0/27, 0/12, 0/22, 0/30, 0/12 and a failed
# dispatch probe.
set -u
source /tests/helpers.sh
require_step 02
harden_reward_dir

CANDIDATE=/app/build/t8n
LOG=/tmp/verifier.log
: > /tmp/verifier-cases.log
exec > >(tee -a "$LOG") 2>&1

echo "=== Step 02 verifier started $(date -u +%Y-%m-%dT%H:%M:%SZ) ==="

GROUPS_NEW="t9n-valid t9n-invalid t9n-intrinsic-gas b11r-assembly b11r-ommers-and-roots tool-dispatch-and-errors"
GROUPS_INHERITED="inherited-london-t8n inherited-berlin-t8n"

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
# The binary keeps the name `t8n` across every step of the ladder: it is one
# `evm` executable with three subcommands, not three programs.
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
    local name="$1" kind="$2" tool="$3" fork="$4" out passed cases
    if [ ! -d "/tests/corpora/$name/cases" ]; then
        echo "VERIFIER: corpus missing for group $name"
        ROWS+=("$name|$kind|0|0")
        return
    fi
    # Two clocks. PER_CASE bounds one invocation; the group clock bounds the
    # whole group at its own worst case, so a candidate whose tool hangs on
    # every case still ends with a scorecard instead of a harness kill -- and a
    # harness kill is what turns "the verifier never finished" into a
    # complete-looking partial score.
    local n cap
    n=$(ls -d "/tests/corpora/$name/cases"/*/ 2>/dev/null | wc -l)
    cap=$(( 90 + ((n + JOBS - 1) / JOBS) * PER_CASE ))
    out=$(JOBS="$JOBS" timeout "$cap" bash /tests/run_group.sh "$name" "$tool" "$fork" \
              "/tests/corpora/$name" "$CANDIDATE" "$PER_CASE" 2>>/tmp/verifier-cases.log)
    passed=${out%% *}; cases=${out##* }
    case "$passed$cases" in
        ''|*[!0-9]*) echo "VERIFIER: group $name produced no summary (group clock $cap s expired, or crash)"
                     passed=0; cases=$n ;;
    esac
    printf 'GROUP %-28s %-10s %s/%s\n' "$name" "$kind" "$passed" "$cases"
    ROWS+=("$name|$kind|$passed|$cases")
}

run_corpus_group t9n-valid             new       t9n  London
run_corpus_group t9n-invalid           new       t9n  London
run_corpus_group t9n-intrinsic-gas     new       t9n  London
run_corpus_group b11r-assembly         new       b11r -
run_corpus_group b11r-ommers-and-roots new       b11r -
run_corpus_group inherited-london-t8n  inherited t8n  London
run_corpus_group inherited-berlin-t8n  inherited t8n  Berlin

# The dispatch group is driven by direct invocations rather than a corpus, but
# it borrows one case from each tool's corpus as a known-good input to probe
# around. The t8n seed must be a case that carries the JSON transaction form --
# the dispatch probe feeds that form to t8n as a positive control and derives
# the "wrong form" document for the t9n/b11r refusals from its case.json -- so
# it is selected by the property, not by position in the corpus.
T9N_CASE=$(ls -d /tests/corpora/t9n-valid/cases/*/ 2>/dev/null | head -1); T9N_CASE=${T9N_CASE%/}
B11R_CASE=$(ls -d /tests/corpora/b11r-assembly/cases/*/ 2>/dev/null | head -1); B11R_CASE=${B11R_CASE%/}
T8N_CASE=$(ls -d /tests/corpora/inherited-london-t8n/cases/*/txs.json 2>/dev/null \
           | head -1); T8N_CASE=${T8N_CASE%/txs.json}
if [ -n "$T9N_CASE" ] && [ -n "$B11R_CASE" ] && [ -n "$T8N_CASE" ]; then
    out=$(timeout 600 bash /tests/dispatch_probe.sh "$CANDIDATE" "$T9N_CASE" "$B11R_CASE" \
              "$T8N_CASE" 2>>/tmp/verifier-cases.log)
    passed=${out%% *}; cases=${out##* }
    case "$passed$cases" in
        ''|*[!0-9]*)
        # A probe group that expires or crashes reports nothing, so pin the
        # denominator to the case count the script is written to emit. Left
        # at 0, the details file would say 0/0 -- which reads as "this group
        # does not exist" rather than "this group produced no verdicts".
        echo "VERIFIER: tool-dispatch-and-errors produced no summary (its 600 s clock expired, or it crashed)"
        passed=0; cases=21 ;;
    esac
    printf 'GROUP %-28s %-10s %s/%s\n' "tool-dispatch-and-errors" "new" "$passed" "$cases"
    ROWS+=("tool-dispatch-and-errors|new|${passed:-0}|${cases:-0}")
else
    echo "VERIFIER: dispatch probe has no seed case"
    ROWS+=("tool-dispatch-and-errors|new|0|0")
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
echo "=== Step 02: reward $REWARD ==="
report "$REWARD"
