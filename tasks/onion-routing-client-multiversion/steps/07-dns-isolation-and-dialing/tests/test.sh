#!/usr/bin/env bash
# steps/07-dns-isolation-and-dialing/tests/test.sh
#
# Every step carries its own copy of this script; the copies differ only in
# STEP_ID, STEP_DIR and CRATE_ROOT.
#
# Scores THIS step's milestone only, in [0, 1], as `reward = gate x score`:
#
#   gate  in {0,1}  every gate group passes completely, or the step scores 0
#   score in [0,1]  weighted pass fraction over the SCORED groups only
#
# The inherited-regression groups are the gate. They are re-run here against the
# cumulative tree, and they earn NOTHING -- keeping earlier behaviour alive is a
# precondition, not a payout. That is what makes a no-op score exactly 0: it
# preserves prior behaviour so gate = 1, and implements nothing new so
# score = 0. There is no per-step "no-op ceiling" to compute, and no inherited
# share for a candidate to bank by doing nothing.
#
# One failing gate case zeroes the step however good the new work is, so a
# gate case must be provably deterministic or must not exist at all.
set -u

STEP_ID="07"
STEP_DIR="07-dns-isolation-and-dialing"
CRATE_ROOT="crates"      # "." at arti-v0.0.0, "crates" from v0.0.1 on

. /tests/helpers.sh
harden_reward_dir
require_step "$STEP_ID"

APP=/app
ASSETS=/tests/assets
WORK=/work                        # driver build area, deliberately OUTSIDE /app
DETAILS=/logs/verifier/reward_details.json
LOGDIR=/logs/verifier/logs
mkdir -p "$WORK" "$LOGDIR"

# Wall-clock ceilings. Their sum plus slack is the derivation behind this
# step's [steps.verifier].timeout_sec in task.toml; changing one here means
# re-deriving that number.
# Measured on the Oracle trees with CARGO_BUILD_JOBS=4: a cold workspace build
# is 83 s at arti-v0.0.0 and 155 s at arti-v1.1.0; a cold driver build is 52 s
# and a warm one 1 s. Each ceiling below is roughly an order of magnitude above
# its measurement, because the thing being bounded is a CANDIDATE's code, which
# may be far slower to compile than the reference, and because a ceiling that
# fires turns a gradeable submission into a zero.
T_CANDIDATE_BUILD=1800
T_RESOLVE=600
T_DRIVER_BUILD=1500
T_ATTRIB_BUILD=900                # whole per-group attribution pass, not each
T_GROUP=150

export CARGO_TARGET_DIR="$WORK/target"
export CARGO_HOME="${CARGO_HOME:-/usr/local/cargo}"
export CARGO_NET_RETRY=3
export RUST_BACKTRACE=1
# Deterministic test output: no colour, no progress bars, stable thread count.
export CARGO_TERM_COLOR=never
export CARGO_TERM_PROGRESS_WHEN=never

# ---------------------------------------------------------------------------
# Result accumulation
# ---------------------------------------------------------------------------
# One line per group:
#   name<TAB>kind<TAB>tier<TAB>weight<TAB>want<TAB>passed<TAB>failed<TAB>ignored<TAB>status
# `kind` is gate|scored and decides whether the row vetoes or earns. It is read
# from groups.txt, never inferred from the tier, so that a mis-tiered group is a
# data error the validator below can catch rather than a silent scoring change.
RESULTS="$WORK/results.tsv"
: > "$RESULTS"

VERIFIER_ERROR=""                 # non-empty => infrastructure fault, not a
                                  # candidate fault; recorded in the details

record() {
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$1" "$2" "$3" "$4" "$5" "$6" "$7" "$8" "$9" >> "$RESULTS"
}

# Build reward_details.json from RESULTS under `reward = gate x score`.
#
# The gate is binary and earns nothing; the score is a weighted pass fraction
# over the scored groups alone. A vetoed step MUST name the gate group that
# vetoed it: otherwise `reward = 0` from a real regression is indistinguishable
# from `reward = 0` from a candidate that implemented nothing.
write_details() {
    local reward="$1"
    awk -v reward="$reward" -v step="$STEP_DIR" -v verr="$VERIFIER_ERROR" \
        -v stubs="${STUBBED_CRATES:-}" -v dropfeats="${DROPPED_FEATURES:-}" \
        -v resolution="${RESOLVED:-none}" '
    BEGIN { FS = "\t"; st_total = 0; st_earned = 0; n = 0; gate_n = 0; gate_bad = 0 }
    {
        name[n] = $1; kind[n] = $2; tier[n] = $3; w[n] = $4 + 0; want[n] = $5 + 0
        pass[n] = $6 + 0; fail[n] = $7 + 0; ign[n] = $8 + 0; st[n] = $9
        if (kind[n] == "gate") {
            gate_n++
            if (st[n] != "pass") { gate_fail[gate_bad] = n; gate_bad++ }
        } else {
            st_total += w[n]
            if (st[n] == "pass") st_earned += w[n]
        }
        tier_total[$3] += w[n]
        if (st[n] == "pass") { tier_earned[$3] += w[n] } else { tier_earned[$3] += 0 }
        n++
    }
    END {
        gate_ok = (gate_bad == 0) ? 1 : 0
        score = (st_total > 0) ? st_earned / st_total : 0
        printf "{\n"
        printf "  \"step\": \"%s\",\n", step
        printf "  \"reward\": %s,\n", reward
        printf "  \"gate\": %d,\n", gate_ok
        printf "  \"score\": %.6f,\n", score
        printf "  \"gate_group_count\": %d,\n", gate_n
        printf "  \"scored_weight_total\": %d,\n", st_total
        printf "  \"scored_weight_earned\": %d,\n", st_earned
        if (verr != "") printf "  \"verifier_error\": \"%s\",\n", verr
        printf "  \"stubbed_crates\": ["
        ns = split(stubs, sa, " ")
        k = 0
        for (i = 1; i <= ns; i++) {
            if (sa[i] == "") continue
            printf "%s\"%s\"", (k ? ", " : ""), sa[i]; k++
        }
        printf "],\n"
        # A dropped feature request is the same class of evidence as a
        # stubbed crate: it records that the candidate tree had not reached
        # this milestone, and it belongs in the machine-readable record
        # rather than only in the log.
        printf "  \"dropped_features\": ["
        nd = split(dropfeats, da, " ")
        k = 0
        for (i = 1; i <= nd; i++) {
            if (da[i] == "") continue
            printf "%s\"%s\"", (k ? ", " : ""), da[i]; k++
        }
        printf "],\n"
        # Which rung of the resolution ladder carried the run. A gate failure
        # reads differently depending on it: under "pinned" the graph is the
        # reviewed one and a failure is about behaviour, while under the later
        # rungs the pinning was loosened to reach the candidate at all.
        printf "  \"resolution\": \"%s\",\n", resolution
        printf "  \"gate_failures\": ["
        for (i = 0; i < gate_bad; i++) {
            g = gate_fail[i]
            printf "%s\n    {\"name\": \"%s\", \"tier\": \"%s\", \"status\": \"%s\", \"want\": %d, \"passed\": %d, \"failed\": %d, \"ignored\": %d}", \
                (i ? "," : ""), name[g], tier[g], st[g], want[g], pass[g], fail[g], ign[g]
        }
        printf "%s],\n", (gate_bad ? "\n  " : "")
        printf "  \"tiers\": {"
        first = 1
        for (t in tier_total) {
            if (!first) printf ","
            printf "\n    \"%s\": {\"weight_total\": %d, \"weight_earned\": %d}", \
                t, tier_total[t], tier_earned[t]
            first = 0
        }
        printf "\n  },\n"
        printf "  \"groups\": [\n"
        for (i = 0; i < n; i++) {
            printf "    {\"name\": \"%s\", \"kind\": \"%s\", \"tier\": \"%s\", \"weight\": %d, \"want\": %d, \"passed\": %d, \"failed\": %d, \"ignored\": %d, \"status\": \"%s\"}%s\n", \
                name[i], kind[i], tier[i], w[i], want[i], pass[i], fail[i], ign[i], st[i], \
                (i == n - 1 ? "" : ",")
        }
        printf "  ]\n}\n"
    }' "$RESULTS" > "$DETAILS"
    chmod 644 "$DETAILS" 2>/dev/null || true
}

# reward = gate x score.
#   gate  : 1 only if EVERY gate group passed completely. Earns nothing.
#   score : weighted pass fraction over the SCORED groups alone. Their weights
#           are relative integers in groups.txt; dividing by the scored total is
#           what makes them sum to 1.0 among themselves, without asking a text
#           manifest to carry floats that must be kept summing to exactly 1.
finish() {
    local gate score reward
    gate=$(awk -F'\t' 'BEGIN { g = 1 } $2 == "gate" && $9 != "pass" { g = 0 } END { print g }' "$RESULTS")
    score=$(awk -F'\t' '
        $2 != "gate" { total += $4; if ($9 == "pass") earned += $4 }
        END {
            if (total <= 0) { printf "0.000000"; exit }
            printf "%.6f", earned / total
        }' "$RESULTS")
    reward=$(awk -v g="$gate" -v s="$score" 'BEGIN { printf "%.6f", g * s }')
    write_details "$reward"
    printf '\n=== step %s: gate %s x score %s = reward %s ===\n' \
        "$STEP_DIR" "$([ "$gate" = 1 ] && echo PASS || echo FAIL)" "$score" "$reward"
    awk -F'\t' '{ printf "  %-34s %-6s %-11s w=%-3s %s (%s/%s passed, %s failed, %s ignored)\n", $1, $2, $3, $4, $9, $6, $5, $7, $8 }' "$RESULTS"
    if [ "$gate" != 1 ]; then
        printf '\nGATE FAILED -- step scores 0 regardless of new work. Vetoing group(s):\n'
        awk -F'\t' '$2 == "gate" && $9 != "pass" { printf "  %-34s %s (%s/%s passed, %s failed, %s ignored)\n", $1, $9, $6, $5, $7, $8 }' "$RESULTS"
    fi
    report "$reward"
}

# Every group scores 0 but the details still name each one, so a failure that
# is really about the harness is distinguishable from a candidate that
# implemented nothing.
fail_everything() {
    local reason="$1" status="${2:-not_run}"
    VERIFIER_ERROR="$reason"
    : > "$RESULTS"
    local i=0
    while [ "$i" -lt "${#ALL_GROUPS[@]}" ]; do
        record "${ALL_GROUPS[$i]}" "${GROUP_KIND[$i]}" "${GROUP_TIER[$i]}" \
               "${GROUP_WEIGHT[$i]}" "${GROUP_WANT[$i]}" 0 0 0 "$status"
        i=$((i + 1))
    done
    printf '\nVERIFIER: %s\n' "$reason" >&2
    finish
}

# Any unexpected exit still writes a reward. Without this a `set -u` slip or an
# OOM kill leaves no reward.json at all, which Harbor reads as a crashed
# verifier and which hides the step's real state.
on_exit() {
    local rc=$?
    [ -f /logs/verifier/reward.json ] && return
    VERIFIER_ERROR="verifier_aborted_rc_${rc}"
    printf '{"reward":0,"verifier_error":"verifier_aborted","exit_code":%d}\n' "$rc" \
        > /logs/verifier/reward.json 2>/dev/null || true
    chmod 755 /logs/verifier 2>/dev/null || true
}
trap on_exit EXIT

say() { printf '\n--- %s\n' "$*"; }

# ---------------------------------------------------------------------------
# Asset integrity
# ---------------------------------------------------------------------------
# The scored groups, their case counts and their weights are data, and every
# number below depends on them. They are checked against SHA256SUMS before
# anything is parsed.
say "asset integrity"
if [ ! -f "$ASSETS/SHA256SUMS" ]; then
    ALL_GROUPS=(); GROUP_KIND=(); GROUP_TIER=(); GROUP_WEIGHT=(); GROUP_WANT=()
    fail_everything "assets_missing_sha256sums"
fi
if ! ( cd "$ASSETS" && sha256sum --quiet -c SHA256SUMS ) > "$LOGDIR/assets.log" 2>&1; then
    ALL_GROUPS=(); GROUP_KIND=(); GROUP_TIER=(); GROUP_WEIGHT=(); GROUP_WANT=()
    fail_everything "assets_checksum_mismatch"
fi
echo "assets verified against SHA256SUMS"

# ---------------------------------------------------------------------------
# groups.txt
# ---------------------------------------------------------------------------
# Columns: <group> <expected case count> <kind> <tier> <weight>
#
#   kind ∈ gate | scored     what the group DOES to the reward
#     gate      binary veto. Earns nothing, ever. Every gate group must pass
#               completely or the step scores 0 however good the new work is.
#               Weight must be 0: a weight on a gate row is a contradiction,
#               not a rounding detail, so it is refused below.
#     scored    earns. Weights are relative integers, normalised by dividing by
#               the scored total, so they sum to 1.0 among themselves.
#
#   tier ∈ proof | inherited | new | peripheral     WHY the group has that kind
#     proof       proof of life: the candidate builds, the artifact starts
#     inherited   one deterministic end-to-end path through a milestone this
#                 step carries forward
#     new         this step's own capability - the bulk of the scored weight
#     peripheral  breadth around the new capability
#
# kind and tier are cross-checked: proof/inherited must be gate, new/peripheral
# must be scored. Recording both and refusing disagreement is what turns a
# mis-tiered group from a silent scoring change into a startup failure. Under
# `reward = gate x score` a new-capability group marked gate becomes an
# unpriced veto, and a gate group marked scored both stops vetoing AND hands a
# no-op free credit.
ALL_GROUPS=(); GROUP_KIND=(); GROUP_TIER=(); GROUP_WEIGHT=(); GROUP_WANT=()
while read -r g want kind tier weight _rest; do
    case "$g" in ''|'#'*) continue ;; esac
    ALL_GROUPS+=("$g"); GROUP_WANT+=("$want")
    GROUP_KIND+=("$kind"); GROUP_TIER+=("$tier"); GROUP_WEIGHT+=("$weight")
done < "$ASSETS/groups.txt"

GROUP_COUNT=${#ALL_GROUPS[@]}
if [ "$GROUP_COUNT" -eq 0 ]; then
    fail_everything "groups_txt_empty"
fi

# Manifest coherence. Each of these is a package defect that would otherwise
# express itself as a wrong reward rather than as an error.
GATE_GROUPS=0; SCORED_GROUPS=0; SCORED_WEIGHT=0
i=0
while [ "$i" -lt "$GROUP_COUNT" ]; do
    g="${ALL_GROUPS[$i]}"; k="${GROUP_KIND[$i]}"
    t="${GROUP_TIER[$i]}"; w="${GROUP_WEIGHT[$i]}"
    case "$k" in
        gate)
            case "$t" in
                proof|inherited) ;;
                *) fail_everything "gate_group_with_scored_tier_${g}_${t}" ;;
            esac
            [ "$w" = "0" ] || fail_everything "gate_group_carries_weight_${g}_${w}"
            GATE_GROUPS=$((GATE_GROUPS + 1))
            ;;
        scored)
            case "$t" in
                new|peripheral) ;;
                *) fail_everything "scored_group_with_gate_tier_${g}_${t}" ;;
            esac
            [ "$w" -gt 0 ] 2>/dev/null || fail_everything "scored_group_without_weight_${g}_${w}"
            SCORED_GROUPS=$((SCORED_GROUPS + 1))
            SCORED_WEIGHT=$((SCORED_WEIGHT + w))
            ;;
        *) fail_everything "unknown_group_kind_${g}_${k}" ;;
    esac
    i=$((i + 1))
done

# Every step has a gate: step 1's is proof of life alone, every later step adds
# its inherited milestones. A step with no gate group cannot veto a regression,
# and the absence would be invisible at grading time because the arithmetic
# still produces a plausible number.
if [ "$GATE_GROUPS" -eq 0 ]; then
    fail_everything "no_gate_groups_declared"
fi
# A step with no scored weight can only ever report 0, so the Oracle would fail
# its own step. Refuse it loudly instead of reporting a mysterious zero.
if [ "$SCORED_GROUPS" -eq 0 ] || [ "$SCORED_WEIGHT" -le 0 ]; then
    fail_everything "no_scored_weight_declared"
fi

# A group name that is a suffix of another group name would make the cargo
# test filter "<group>__" match the other group's cases too, and the
# executed-count assertion below would then fire on a correct candidate.
# Cheaper to refuse the configuration than to debug that at grading time.
i=0
while [ "$i" -lt "$GROUP_COUNT" ]; do
    j=0
    while [ "$j" -lt "$GROUP_COUNT" ]; do
        if [ "$i" -ne "$j" ]; then
            case "${ALL_GROUPS[$j]}" in
                *"${ALL_GROUPS[$i]}")
                    fail_everything "group_name_suffix_collision_${ALL_GROUPS[$i]}_${ALL_GROUPS[$j]}" ;;
            esac
        fi
        j=$((j + 1))
    done
    i=$((i + 1))
done

# Three-way cross-check: groups.txt, the shipped counts, and the driver source
# must agree. Any two of them agreeing while the third drifts is the failure
# mode this catches - most often a group whose cases were edited without its
# expected count being updated, which would otherwise silently fail a correct
# candidate.
WANT_SUM=0
for w in "${GROUP_WANT[@]}"; do WANT_SUM=$((WANT_SUM + w)); done
CASE_TOTAL=$(cat "$ASSETS/CASE_TOTAL" 2>/dev/null || echo -1)
DECLARED_GROUPS=$(cat "$ASSETS/GROUP_COUNT" 2>/dev/null || echo -1)
SRC_CASES=$(grep -rhoE '^\s*fn [a-z0-9_]+__[a-z0-9_]+\s*\(' \
    "$ASSETS/harness/tests/groups" 2>/dev/null | wc -l)

if [ "$CASE_TOTAL" != "$WANT_SUM" ]; then
    fail_everything "case_total_mismatch_declared_${CASE_TOTAL}_groupstxt_${WANT_SUM}"
fi
if [ "$DECLARED_GROUPS" != "$GROUP_COUNT" ]; then
    fail_everything "group_count_mismatch_declared_${DECLARED_GROUPS}_groupstxt_${GROUP_COUNT}"
fi
if [ "$SRC_CASES" != "$WANT_SUM" ]; then
    fail_everything "source_case_count_mismatch_source_${SRC_CASES}_groupstxt_${WANT_SUM}"
fi
echo "groups: $GROUP_COUNT, cases: $WANT_SUM (groups.txt == CASE_TOTAL == driver source)"
echo "gate: $GATE_GROUPS group(s) (veto only, earn nothing); scored: $SCORED_GROUPS group(s), relative weight $SCORED_WEIGHT"

# ---------------------------------------------------------------------------
# Candidate tree sanity
# ---------------------------------------------------------------------------
say "candidate tree"
if [ ! -f "$APP/Cargo.toml" ]; then
    fail_everything "candidate_missing_workspace_manifest"
fi
APP_LAYOUT=$(candidate_crate_root "$APP")
echo "expected crate root: $CRATE_ROOT ; observed: $APP_LAYOUT"
if [ "$APP_LAYOUT" != "$CRATE_ROOT" ]; then
    # Not fatal on its own - the driver manifest will fail to resolve and every
    # group will score 0 with a legible reason - but say so plainly here.
    echo "WARNING: candidate crate layout differs from the layout documented in TEST_INTERFACE.md" >&2
fi
APP_MANIFEST_SUM_BEFORE=$(find "$APP" -name Cargo.toml -not -path '*/target/*' \
    -exec sha256sum {} + 2>/dev/null | sort | sha256sum | cut -d' ' -f1)

give_to_unprivileged "$APP"
give_to_unprivileged "$WORK"

# The candidate's own build. Its failure is not by itself fatal - the driver
# build below is the authoritative compile - but running it first gives a clean
# "does their workspace compile at all" line in the log and warms the shared
# target directory.
#
# --offline is required here. Grading runs with network_mode = "public", so
# without it this build could reach crates.io, and whatever it downloaded would
# land in the shared CARGO_HOME that every --offline resolution attempt below
# reads. The graded resolve would then depend on the network by a side door:
# the same candidate could be gradeable or not according to what the registry
# served that minute.
say "candidate workspace build (diagnostic)"
CAND_BUILD_RC=0
run_unprivileged env CARGO_TARGET_DIR="$CARGO_TARGET_DIR" \
    timeout "$T_CANDIDATE_BUILD" cargo build --manifest-path "$APP/Cargo.toml" \
    --workspace --offline --jobs 4 > "$LOGDIR/candidate-build.log" 2>&1 || CAND_BUILD_RC=$?
echo "candidate workspace build rc=$CAND_BUILD_RC (see logs/candidate-build.log)"

# ---------------------------------------------------------------------------
# Stage the driver
# ---------------------------------------------------------------------------
# Built outside /app and path-linked to the candidate's crates, so the scored
# code is reached only through the public boundary TEST_INTERFACE.md documents
# and the candidate's own manifest is never edited.
say "stage driver"
rm -rf "$WORK/driver"
cp -a "$ASSETS/harness" "$WORK/driver"
# The shipped manifest names the candidate tree as @@APP@@ so that the
# checksummed bytes do not depend on where the tree lives. Substituted on the
# staged copy only; the asset itself is never rewritten.
sed -i "s|@@APP@@|$APP|g" "$WORK/driver/Cargo.toml"
give_to_unprivileged "$WORK"

DRIVER="$WORK/driver"
CARGO_COMMON=(--manifest-path "$DRIVER/Cargo.toml" --jobs 4)

# ---------------------------------------------------------------------------
# Missing-crate repair
# ---------------------------------------------------------------------------
# One driver manifest serves every group at this step, so a path dependency on
# a crate the candidate never created is not a failure of one group: cargo
# cannot resolve the manifest at all, and every group -- gate included -- scores
# zero. At a step whose instruction says "add crate X" that is an unpriced veto
# of exactly the shape the gate exists to avoid. The step then reports 0 through
# the GATE rather than through the SCORE, which is indistinguishable from a
# candidate that broke inherited behaviour.
#
# Substituting an empty crate restores per-group isolation at the manifest layer,
# the same way the generated attribution targets restore it at the module layer.
# It is monotone-safe: a lib with no items can only turn a compile that would
# have succeeded into one that fails, never the reverse, so no group can pass
# THROUGH a stub. Groups that need crate X still fail on X's absent symbols and
# still earn nothing; groups that never mention X are graded on their own merits.
#
# A milestone that RELOCATES the crate tree -- v0.0.1 moved every crate under
# crates/ and renamed tor-client to arti-client -- leaves a candidate who has
# not yet made that transition with every path dependency pointing into a
# directory that does not exist. Cargo resolves the whole graph before running
# any case, so that candidate scores 0 with every group recorded not_run,
# INCLUDING the gate groups, which test the PREVIOUS checkpoint's behaviour and
# by construction cannot depend on this step's relocation. Under
# `reward = gate x score` that is a veto of the whole reward. A changed layout
# is new capability, so pricing it into the gate is the unpriced veto in
# another form -- one no sweep of groups.txt can find, because the group is
# tiered correctly and simply cannot be reached.
#
# So resolve the driver against the layout that is actually there, then stub
# whatever is still genuinely absent. This changes nothing when the layouts
# agree.
say "missing-crate repair"
STUBBED_CRATES=""
REROOTED=""
if [ "$APP_LAYOUT" != "$CRATE_ROOT" ]; then
    if [ "$CRATE_ROOT" = "." ]; then FROM="$APP"; else FROM="$APP/$CRATE_ROOT"; fi
    if [ "$APP_LAYOUT" = "." ]; then TO="$APP"; else TO="$APP/$APP_LAYOUT"; fi
    sed -i "s|path = \"$FROM/|path = \"$TO/|g" "$DRIVER/Cargo.toml"
    REROOTED="$FROM/ -> $TO/"
    echo "re-rooted driver path dependencies: $REROOTED"
    echo "  (candidate has not made this step's relocation; the gate groups test the"
    echo "   predecessor's behaviour and must still be reachable)"
fi
STUBS="$WORK/stubs"
mkdir -p "$STUBS"
while read -r dep dpath dfeats; do
    [ -n "$dep" ] || continue
    [ -f "$dpath/Cargo.toml" ] && continue
    mkdir -p "$STUBS/$dep/src"
    : > "$STUBS/$dep/src/lib.rs"
    {
        printf '[package]\nname = "%s"\nversion = "0.0.0"\nedition = "2018"\n\n[features]\n' "$dep"
        # A feature the driver names must exist on the stub or resolution
        # still fails on the feature rather than on the crate. Declared
        # empty: present, enabling nothing.
        for f in $dfeats; do printf '%s = []\n' "$f"; done
    } > "$STUBS/$dep/Cargo.toml"
    sed -i "s|path = \"$dpath\"|path = \"$STUBS/$dep\"|" "$DRIVER/Cargo.toml"
    STUBBED_CRATES="$STUBBED_CRATES $dep"
done < <(awk '
    /^[A-Za-z0-9_-]+[ \t]*=[ \t]*\{/ {
        if (!match($0, /path[ \t]*=[ \t]*"[^"]*"/)) next
        p = substr($0, RSTART, RLENGTH)
        sub(/^path[ \t]*=[ \t]*"/, "", p); sub(/"$/, "", p)
        n = $0; sub(/[ \t]*=.*/, "", n)
        feats = ""
        if (match($0, /features[ \t]*=[ \t]*\[[^]]*\]/)) {
            f = substr($0, RSTART, RLENGTH)
            sub(/^features[ \t]*=[ \t]*\[/, "", f); sub(/\]$/, "", f)
            gsub(/[",]/, " ", f)
            feats = f
        }
        print n, p, feats
    }' "$DRIVER/Cargo.toml")
give_to_unprivileged "$WORK"
# The third way this step's own novelty can veto its own gate, after a crate
# that does not exist and a crate that has moved: a crate that is present, at
# the right path, which does not yet declare a CARGO FEATURE this milestone
# adds. v0.0.2 gives tor-config `expand-paths`; against v0.0.1 the crate is
# there and the feature is not, and cargo fails feature selection for the whole
# graph. No case runs, and the gate vetoes on new capability instead of on
# regressed behaviour -- the same fault as the other two.
#
# Dropping a feature request the target does not declare is inert wherever it
# does declare it. It is monotone-safe for the reason the empty stub is: not
# enabling a feature cannot turn a failing compile into a passing one, so no
# group passes THROUGH the repair. Groups that need the feature's items still
# fail on those items.
DROPPED_FEATURES=""
while read -r dep dpath dfeats; do
    [ -n "$dep" ] || continue
    [ -n "$dfeats" ] || continue
    [ -f "$dpath/Cargo.toml" ] || continue
    avail=$({
        awk '/^\[features\]/ { f=1; next } /^\[/ { f=0 }
             f && /^[A-Za-z0-9_.-]+[ \t]*=/ { n=$0; sub(/[ \t]*=.*/,"",n); gsub(/[ \t]/,"",n); print n }' \
            "$dpath/Cargo.toml"
        awk '/^\[.*dependencies\]/ { d=1; next } /^\[/ { d=0 }
             d && /optional[ \t]*=[ \t]*true/ { n=$0; sub(/[ \t]*=.*/,"",n); gsub(/[ \t]/,"",n); print n }' \
            "$dpath/Cargo.toml"
    })
    for f in $dfeats; do
        printf '%s\n' "$avail" | grep -qxF "$f" && continue
        sed -i "/^[ \t]*$dep[ \t]*=/ s/\"$f\"[ \t]*,\{0,1\}[ \t]*//" "$DRIVER/Cargo.toml"
        DROPPED_FEATURES="$DROPPED_FEATURES $dep/$f"
    done
done < <(awk '
    /^[A-Za-z0-9_-]+[ \t]*=[ \t]*\{/ {
        if (!match($0, /path[ \t]*=[ \t]*"[^"]*"/)) next
        p = substr($0, RSTART, RLENGTH)
        sub(/^path[ \t]*=[ \t]*"/, "", p); sub(/"$/, "", p)
        n = $0; sub(/[ \t]*=.*/, "", n)
        feats = ""
        if (match($0, /features[ \t]*=[ \t]*\[[^]]*\]/)) {
            f = substr($0, RSTART, RLENGTH)
            sub(/^features[ \t]*=[ \t]*\[/, "", f); sub(/\]$/, "", f)
            gsub(/[",]/, " ", f)
            feats = f
        }
        print n, p, feats
    }' "$DRIVER/Cargo.toml")
# A removal can leave "[\"a\", ]"; cargo tolerates it, a reader should not.
sed -i 's/,[ \t]*\]/]/g' "$DRIVER/Cargo.toml"
if [ -n "$DROPPED_FEATURES" ]; then
    echo "candidate's crates do not declare:$DROPPED_FEATURES -- dropped those feature"
    echo "  requests so the groups that do not need them can still be graded; groups"
    echo "  that do will fail on the items the feature would have gated."
fi

if [ -n "$STUBBED_CRATES" ]; then
    echo "candidate did not create:$STUBBED_CRATES -- substituted empty crates so the"
    echo "  groups that do not use them can still be graded; groups that do will fail."
else
    echo "every path dependency named by the driver resolves to a crate the candidate"
    echo "  provides (checked; this is not the same as not having looked)"
fi

# Dependency resolution, in decreasing order of determinism.
#   1. the candidate's own Cargo.lock, offline, with the reviewed lockfile's
#      entries added for every package name it does not mention. The driver
#      compiles the CANDIDATE's crates, so the graph that has to hold is the
#      graph those crates were built and tested against; cargo keeps every
#      entry that lock still satisfies, and the reviewed entries fill in only
#      what the candidate's file never named -- the driver's own dependencies;
#   2. the lockfile this step was reviewed and Oracle-tested with, offline and
#      --locked: exact, and the path a tree with no lockfile at all takes;
#   3. the same lockfile with --locked dropped, offline: cargo keeps every
#      version that still satisfies its requirement and updates only what must
#      change. A candidate whose graph differs from this step's -- which is
#      every candidate that has not yet made this step's transition, the no-op
#      included -- resolves here, deterministically, from the same universe;
#   4. the lockfile discarded, offline: a full re-resolve from the universe
#      baked into the image.
# There is no online attempt: a non-deterministic case is not a weaker case
# but an unusable one, and resolving against the live index makes the whole
# step depend on what crates.io serves that day. A live resolve can, for
# example, pull a modern x25519-dalek, which no longer has StaticSecret, so the
# candidate's own tor-llcrypto fails to compile and every group, gate included,
# comes back compile_error. Attempt 3 exists because attempt 4 discards the
# pinning that prevents exactly that.
# Whichever succeeds, the driver source is identical, so the scored assertions
# do not change with the path taken.
#
# Why attempt 1 comes first. A workspace shipped at the first rung carries that
# rung's Cargo.lock, whose pins are newer than the later releases' own, and
# cargo never moves a locked version DOWN while the requirement still allows
# it. A candidate who migrates the manifests and rebuilds therefore keeps those
# pins (for example futures-channel 0.3.34, ed25519 1.5.3 and signature 1.6.4,
# where this release's lock has 0.3.25, 1.2.0 and 1.4.0), and its crates may
# call API that exists only in the newer versions (futures-channel's
# `UnboundedReceiver::try_recv`; ed25519's inherent `Signature::from_bytes`).
# Resolving the driver against this release's lock would then fail every group
# with compile_error although the candidate's own workspace builds clean -- a
# step failing for a reason that is not the candidate's behaviour, which is the
# one thing the gate may never do. Grading the tree against the graph the tree
# itself resolves to avoids that: the versions it was built against are the
# versions it is graded at, and a wrong implementation still fails, because no
# assertion moves.
say "driver dependency resolution"
RESOLVED=""
resolve_attempt() {
    local label="$1"; shift
    if run_unprivileged env CARGO_TARGET_DIR="$CARGO_TARGET_DIR" \
        timeout "$T_RESOLVE" cargo metadata --manifest-path "$DRIVER/Cargo.toml" \
        --format-version 1 "$@" > "$LOGDIR/resolve-$label.log" 2>&1; then
        RESOLVED="$label"
        echo "dependency resolution: $label"
        return 0
    fi
    echo "dependency resolution attempt '$label' failed (logs/resolve-$label.log)"
    return 1
}

# The reviewed lockfile's [[package]] blocks whose package NAME the candidate's
# own lockfile does not mention. Text-level on purpose: a lockfile is a cargo
# artifact, not a program, and the only property this needs is "the candidate's
# entries are what they were, and this name is not among them". A name present
# in the candidate's lock is never re-added, so the merged file cannot carry
# two versions of the same crate just because the reviewed file pins another.
reviewed_entries_the_candidate_lacks() {
    local cand="$1" reviewed="$2" names
    names=$(awk 'BEGIN { RS = "" }
        match($0, /name = "[^"]+"/) { print substr($0, RSTART + 8, RLENGTH - 9) }' \
        "$cand" | sort -u)
    awk -v names="$names" '
        BEGIN { RS = ""; n = split(names, a, "\n")
                for (i = 1; i <= n; i++) seen[a[i]] = 1 }
        NR == 1 { next }                      # the reviewed file header
        { nm = ""
          if (match($0, /name = "[^"]+"/)) nm = substr($0, RSTART + 8, RLENGTH - 9)
          if (nm in seen) next
          printf "\n%s\n", $0 }' "$reviewed"
}

cp -f "$DRIVER/Cargo.lock.fallback" "$DRIVER/Cargo.lock" 2>/dev/null || true
cp -f "$DRIVER/Cargo.lock" "$WORK/driver-lock.reviewed" 2>/dev/null || true
give_to_unprivileged "$WORK"

if [ -f "$APP/Cargo.lock" ]; then
    # The candidate's graph first: every entry it names is kept, and the
    # reviewed file only supplies the names it never had.
    { cat "$APP/Cargo.lock"
      reviewed_entries_the_candidate_lacks "$APP/Cargo.lock" "$WORK/driver-lock.reviewed"
    } > "$DRIVER/Cargo.lock.candidate"
    mv -f "$DRIVER/Cargo.lock.candidate" "$DRIVER/Cargo.lock"
    give_to_unprivileged "$WORK"
    resolve_attempt candidate-lock --offline || true
fi
if [ -z "$RESOLVED" ]; then
    # No usable candidate lockfile (absent, or it could not resolve). Fall back
    # to the lockfile this step was reviewed and Oracle-tested with, exactly.
    cp -f "$WORK/driver-lock.reviewed" "$DRIVER/Cargo.lock"
    give_to_unprivileged "$WORK"
    resolve_attempt pinned --locked --offline || true
fi
if [ -z "$RESOLVED" ]; then
    # The candidate moved a dependency version, so the reviewed lockfile no
    # longer describes the graph exactly. Let cargo update the minimum it must
    # while the lockfile still pins everything else.
    resolve_attempt relaxed-lock-offline --offline || true
fi
if [ -z "$RESOLVED" ]; then
    # The reviewed lockfile was resolved against the tree this step is meant to
    # produce. A candidate whose tree still requires a dependency that this
    # step drops gets a lockfile with a hole in it: the dropped crate is
    # unpinned and resolves to its newest cached version, which then demands
    # newer transitives than the entries still pinned around it. No amount of
    # relaxing OUR lockfile fixes that, because the conflict is between what it
    # pins and what it omits. Discard the pinning entirely and re-resolve from
    # the universe baked into the image.
    rm -f "$DRIVER/Cargo.lock"
    give_to_unprivileged "$WORK"
    resolve_attempt relaxed-offline --offline || true
fi

if [ -z "$RESOLVED" ]; then
    fail_everything "driver_dependency_resolution_failed"
fi

LOCKFLAG=()
if [ "$RESOLVED" = "pinned" ]; then
    LOCKFLAG=(--locked)
fi
# Every resolution attempt is offline, so every build below is too.
OFFLINEFLAG=(--offline)

# ---------------------------------------------------------------------------
# Build the aggregate conformance target
# ---------------------------------------------------------------------------
say "driver build"
AGG_OK=0
if run_unprivileged env CARGO_TARGET_DIR="$CARGO_TARGET_DIR" \
    timeout "$T_DRIVER_BUILD" cargo test "${CARGO_COMMON[@]}" \
    "${LOCKFLAG[@]}" "${OFFLINEFLAG[@]}" --test conformance --no-run \
    > "$LOGDIR/driver-build.log" 2>&1; then
    AGG_OK=1
    echo "aggregate conformance target built"
else
    echo "aggregate conformance target did NOT build; falling back to per-group attribution"
fi

# When the aggregate target does not compile, one missing or mis-typed public
# item would otherwise zero every group - including groups that have nothing to
# do with it. Each group also ships as its own test target, so attribution
# compiles let a candidate that implemented most of the milestone keep the
# credit it earned. Bounded as a whole, not per group.
declare -A GROUP_BUILT=()
if [ "$AGG_OK" -eq 1 ]; then
    for g in "${ALL_GROUPS[@]}"; do GROUP_BUILT[$g]=aggregate; done
else
    ATTRIB_DEADLINE=$(( $(date +%s) + T_ATTRIB_BUILD ))
    for g in "${ALL_GROUPS[@]}"; do
        now=$(date +%s)
        if [ "$now" -ge "$ATTRIB_DEADLINE" ]; then
            GROUP_BUILT[$g]=budget_exhausted
            continue
        fi
        remaining=$((ATTRIB_DEADLINE - now))
        if run_unprivileged env CARGO_TARGET_DIR="$CARGO_TARGET_DIR" \
            timeout "$remaining" cargo test "${CARGO_COMMON[@]}" \
            "${LOCKFLAG[@]}" "${OFFLINEFLAG[@]}" --test "g_$g" --no-run \
            > "$LOGDIR/attrib-$g.log" 2>&1; then
            GROUP_BUILT[$g]=solo
        else
            GROUP_BUILT[$g]=compile_error
        fi
    done
fi

# ---------------------------------------------------------------------------
# Run the scored groups
# ---------------------------------------------------------------------------
# Every group runs regardless of what happened to the ones before it: a step's
# score is a profile of which capabilities are present, and stopping at the
# first failure would collapse that profile to "the first thing that broke".
say "groups (gate groups veto; scored groups earn)"
idx=0
while [ "$idx" -lt "$GROUP_COUNT" ]; do
    g="${ALL_GROUPS[$idx]}"
    kind="${GROUP_KIND[$idx]}"
    tier="${GROUP_TIER[$idx]}"
    weight="${GROUP_WEIGHT[$idx]}"
    want="${GROUP_WANT[$idx]}"
    idx=$((idx + 1))

    built="${GROUP_BUILT[$g]:-compile_error}"
    if [ "$built" = "compile_error" ] || [ "$built" = "budget_exhausted" ]; then
        record "$g" "$kind" "$tier" "$weight" "$want" 0 0 0 "$built"
        printf '  %-34s %s\n' "$g" "$built"
        continue
    fi

    target=conformance
    [ "$built" = "solo" ] && target="g_$g"

    log="$LOGDIR/group-$g.log"
    rc=0
    run_unprivileged env CARGO_TARGET_DIR="$CARGO_TARGET_DIR" \
        timeout "$T_GROUP" cargo test "${CARGO_COMMON[@]}" \
        "${LOCKFLAG[@]}" "${OFFLINEFLAG[@]}" --test "$target" \
        -- --test-threads=1 "${g}__" > "$log" 2>&1 || rc=$?

    if [ "$rc" -eq 124 ]; then
        record "$g" "$kind" "$tier" "$weight" "$want" 0 0 0 "timeout"
        printf '  %-34s timeout after %ss\n' "$g" "$T_GROUP"
        continue
    fi

    # "test result: ok. 9 passed; 0 failed; 0 ignored; 0 measured; 41 filtered out"
    read -r passed failed ignored <<< "$(awk '
        /^test result:/ {
            p = f = i = 0
            for (k = 1; k <= NF; k++) {
                if ($(k+1) ~ /^passed/)  p = $k + 0
                if ($(k+1) ~ /^failed/)  f = $k + 0
                if ($(k+1) ~ /^ignored/) i = $k + 0
            }
            tp += p; tf += f; ti += i; seen = 1
        }
        END { if (seen) printf "%d %d %d", tp, tf, ti; else printf "-1 -1 -1" }' "$log")"

    if [ "$passed" -lt 0 ]; then
        # No result line at all: the binary aborted, was killed, or panicked
        # outside a test. Not a pass under any reading.
        record "$g" "$kind" "$tier" "$weight" "$want" 0 0 0 "no_result"
        printf '  %-34s no test-result line (rc=%s)\n' "$g" "$rc"
        continue
    fi

    # The executed-count assertion. A filter that silently matches nothing
    # reports "0 passed; 0 failed" and exits 0 - indistinguishable from success
    # by exit code alone. Requiring passed == want, and refusing ignored cases,
    # is what makes a green group mean "all of this group's cases ran and
    # passed" rather than "nothing objected".
    status=fail
    if [ "$passed" -eq "$want" ] && [ "$failed" -eq 0 ] && [ "$ignored" -eq 0 ] && [ "$rc" -eq 0 ]; then
        status=pass
    elif [ "$passed" -ne "$want" ] && [ "$failed" -eq 0 ] && [ "$rc" -eq 0 ]; then
        status=case_count_mismatch
    fi
    record "$g" "$kind" "$tier" "$weight" "$want" "$passed" "$failed" "$ignored" "$status"
    printf '  %-34s %-20s %s/%s passed, %s failed, %s ignored\n' \
        "$g" "$status" "$passed" "$want" "$failed" "$ignored"
done

# ---------------------------------------------------------------------------
# The driver must not have touched the candidate's manifests
# ---------------------------------------------------------------------------
APP_MANIFEST_SUM_AFTER=$(find "$APP" -name Cargo.toml -not -path '*/target/*' \
    -exec sha256sum {} + 2>/dev/null | sort | sha256sum | cut -d' ' -f1)
if [ "$APP_MANIFEST_SUM_BEFORE" != "$APP_MANIFEST_SUM_AFTER" ]; then
    # Report it rather than hide it; the scores above are still the ones
    # observed.
    echo "WARNING: candidate manifests changed during grading" >&2
    VERIFIER_ERROR="candidate_manifest_mutated_during_grading"
fi

finish
