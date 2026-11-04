#!/usr/bin/env bash
# --- verifier/step binding check -------------------------------------------
# /etc/harbor-step-id names the image; the literal below names this tests/ tree.
# They disagree only when the build context came from another step's cache
# entry, in which case every check below would grade the wrong milestone.
# Reported, not crashed: a non-zero exit makes Harbor skip the remaining steps
# and buries the fault under a second symptom.
# --- end binding check ------------------------------------------------------
#
# Verifier entry point for step 05 (05-outsourced-proofs), certificate-transparency-go v1.0.17.
#
# Generated from misc/certificate-transparency-multiversion/harness/test.sh by
# build-tests.sh. Every step shares this file; what differs between steps is
# assets/groups.txt, assets/groups/, assets/fixtures/ and assets/gomod/.
#
# Runs in this step's verifier container on a fresh copy of /app. Nothing
# written here reaches the next step; only /logs/verifier is read back.
#
# Scoring is step-local and gated:
#
#     gate   in {0,1}   every GATE group passes completely, or the step is zero
#     score  in [0,1]   weighted pass fraction over the SCORED groups only
#     reward = gate * score
#
# GATE groups are proof of life (the candidate builds, the artifact starts)
# plus inherited regression: one deterministic end-to-end path per milestone
# carried forward. They earn nothing -- they only veto. SCORED groups are this
# step's new capability plus peripheral breadth, and their weights are
# normalised to sum to 1.0 among themselves at scoring time.
#
# A no-op therefore scores exactly 0: it implements nothing new, so score=0
# and reward=0 whatever the gate does. Where prior behaviour is preserved and
# the inherited tree still compiles under this step's pinned go.mod, the gate
# passes and the step reports 0 from the scored tier; where that manifest has
# stopped providing a module the inherited tree still imports, the tree does
# not build and the step reports a veto instead. Either way there is no
# per-step ceiling to compute and no inherited share to subtract.
#
# The cliff is deliberate: one failing gate case zeroes the step however good
# the new work is. That is why a gate case must be provably deterministic --
# see task-requirements-and-quality.md "The gate". Because the cliff is
# all-or-nothing, reward_details.json must name the gate group that vetoed and
# why: a step reporting 0 without naming its veto is indistinguishable from a
# candidate that did nothing.
#
# task.toml averages the thirteen steps (multi_step_reward_strategy = "mean").
# No step sets min_reward, so nothing here can abort the trial.
set -u

STEP='05'
RELEASE='v1.0.17'
# The end-to-end corpus: the whole-personality boundary for this step. It is a
# GATE group -- it earns nothing and vetoes the step if it fails. Its kind is
# asserted against the manifest below rather than assumed from its name.
GATE_GROUP='gate-endtoend'

. /tests/helpers.sh

# Before hardening, and before anything else: this runs no candidate code, and
# a mis-bound image should bail rather than grade the wrong milestone.
require_step "$STEP"

# Lock the reward directory before anything else. Candidate code only ever runs
# under run_unprivileged, so it cannot write the score.
harden_reward_dir

A=/tests/assets
W=/tests/work
log_dir=/tmp/verifier-logs
rm -rf "$log_dir" "$W"
mkdir -p "$log_dir/case-logs" "$W/bin"

# A group announces its results with lines carrying this per-run nonce. Product
# code the group drives cannot forge them, so a candidate cannot print itself a
# pass. See TEST_INTERFACE.md section 3.2.
CT_VERIFY_NONCE=$(head -c 16 /dev/urandom | od -An -tx1 | tr -d ' \n')
export CT_VERIFY_NONCE

passed=0
total=0
recorded=','
finished=0
# Per-group outcome, filled by record() and consumed by finish(). Kind and
# weight come from the manifest (groups.txt), never from the group's name.
declare -A GROUP_RESULT=()
declare -A GROUP_DETAIL=()
GROUP_ORDER=()
# First gate group to fail, and why. Reported verbatim in reward_details.json.
gate_veto=''
gate_veto_why=''
gate_veto_list=''

record() {  # record <group> <0=pass|1=fail> <detail>
    local name="$1" rc="$2" detail="${3:-}" bit=0 kind
    total=$((total + 1))
    if [ "$rc" -eq 0 ]; then
        passed=$((passed + 1)); bit=1
    fi
    kind=${GROUP_KIND[$name]:-scored}
    GROUP_RESULT[$name]=$bit
    GROUP_DETAIL[$name]=$detail
    GROUP_ORDER+=("$name")
    # A failing gate group vetoes the step. Keep the first one as the headline
    # and every one of them in the list, so the details name the real cause
    # even when a single root fault fails several gate groups at once.
    if [ "$kind" = gate ] && [ "$bit" -eq 0 ]; then
        if [ -z "$gate_veto" ]; then
            gate_veto="$name"
            gate_veto_why="$detail"
        fi
        gate_veto_list="${gate_veto_list}${gate_veto_list:+,}\"$name\""
    fi
    recorded="${recorded}${name},"
    # Field order is load-bearing: finish() and probe-step.sh both read this
    # positionally as `<status> <name> ...`, so the name stays in field 2 and
    # the kind goes after it.
    printf '%s %s %s %s\n' "$([ "$bit" -eq 1 ] && echo PASS || echo FAIL)" "$name" "$kind" "$detail" \
        >> "$log_dir/groups.txt"
}

# Emit `<name> <kind> <weight> <pass-bit>` for every recorded group. The single
# source of truth for the arithmetic in finish(); group names are directory
# names, so they never contain whitespace.
group_table() {
    local g
    for g in ${GROUP_ORDER[@]+"${GROUP_ORDER[@]}"}; do
        printf '%s %s %s %s\n' \
            "$g" "${GROUP_KIND[$g]:-scored}" "${GROUP_WEIGHT[$g]:-1}" "${GROUP_RESULT[$g]:-0}"
    done
}

write_details() {  # write_details <gate> <score> <reward>
    local gate="$1" score="$2" reward="$3" groups_json veto_json
    # Normalised weight per scored group, so the weights in the details sum to
    # 1.0 exactly as the contract states. Gate groups carry weight 0: they earn
    # nothing, and printing their raw weight would imply otherwise.
    groups_json=$(group_table | awk '
        { name[NR]=$1; kind[NR]=$2; w[NR]=$3; pass[NR]=$4; if ($2!="gate") wsum+=$3 }
        END {
            out=""
            for (i=1; i<=NR; i++) {
                nw = (kind[i]=="gate" || wsum<=0) ? 0 : w[i]/wsum
                out = out (out==""?"":",") \
                      sprintf("\"%s\":{\"pass\":%d,\"kind\":\"%s\",\"weight\":%.6f}", \
                              name[i], pass[i], kind[i], nw)
            }
            printf "%s", out
        }')
    veto_json="null"
    [ -n "$gate_veto" ] && veto_json=$(printf '{"group":"%s","why":"%s"}' \
        "$gate_veto" "$(printf '%s' "$gate_veto_why" | tr -d '"\\' | tr '\n' ' ')")
    printf '{"step":"%s","release":"%s","reward":%s,"gate":%s,"score":%s,"gate_failed":[%s],"gate_veto":%s,"passed_groups":%s,"total_groups":%s,"gate_groups":{"passed":%s,"total":%s},"scored_groups":{"passed":%s,"total":%s},"groups":{%s}}\n' \
        "$STEP" "$RELEASE" "$reward" "$gate" "$score" \
        "$gate_veto_list" "$veto_json" \
        "$passed" "$total" \
        "$gate_pass_n" "$gate_total_n" "$scored_pass_n" "$scored_total_n" \
        "$groups_json" \
        > /logs/verifier/reward_details.json
}

finish() {  # finish   (no arguments: reward = gate * score, both derived here)
    local score b gate reward calc
    finished=1
    cp "$log_dir/groups.txt" /logs/verifier/groups.txt 2>/dev/null || true
    mkdir -p /logs/verifier/failed
    if [ -f "$log_dir/groups.txt" ]; then
        while read -r status name _rest; do
            [ "$status" = FAIL ] || continue
            [ -f "$log_dir/case-logs/$name.log" ] && \
                tail -c 20000 "$log_dir/case-logs/$name.log" > "/logs/verifier/failed/$name.log"
        done < "$log_dir/groups.txt"
    fi
    for b in assets build module; do
        [ -s "$log_dir/$b.log" ] && tail -c 20000 "$log_dir/$b.log" > "/logs/verifier/$b.log"
    done
    # reward = gate * score.
    #
    #   gate  1 only if every GATE group passed; gate groups earn nothing.
    #   score weighted pass fraction over SCORED groups, weights normalised to
    #         sum to 1.0 among themselves.
    #
    # No divisor by the number of task steps: Harbor's
    # multi_step_reward_strategy = "mean" averages the thirteen steps itself.
    #
    # A step with no scored group can never pay out, which would be an
    # authoring fault rather than a candidate result; the inventory check
    # rejects that at startup, and score=0 here is the belt-and-braces case.
    calc=$(group_table | awk '
        $2=="gate" { gt++; if ($4==0) gf++; else gp++; next }
                   { st++; wsum+=$3; if ($4==1) { wpass+=$3; sp++ } }
        END {
            gate  = (gf>0 ? 0 : 1)
            score = (wsum>0 ? wpass/wsum : 0)
            printf "%d %.6f %.6f %d %d %d %d", \
                   gate, score, gate*score, gp+0, gt+0, sp+0, st+0
        }')
    read -r gate score reward gate_pass_n gate_total_n scored_pass_n scored_total_n <<<"$calc"
    write_details "$gate" "$score" "$reward"
    report "$reward"
}

fail_everything() {  # fail_everything <reason>
    : > "$log_dir/groups.txt"
    passed=0; total=0; recorded=','
    GROUP_RESULT=(); GROUP_DETAIL=(); GROUP_ORDER=()
    gate_veto=''; gate_veto_why=''; gate_veto_list=''
    local g
    for g in "${GROUP_NAMES[@]}"; do record "$g" 1 "$1"; done
    finish
}

# If the run ends without reaching finish - a crash, a kill, an unexpected
# `exit` - record every group that was never reached as a failure and report,
# so the result is a complete scorecard rather than a missing reward.json.
# `finish` calls `report`, which always exits 0, so this runs at most once.
on_exit() {
    [ "$finished" -eq 0 ] || return 0
    local g
    for g in "${GROUP_NAMES[@]}"; do
        case "$recorded" in *",$g,"*) continue ;; esac
        record "$g" 1 "verifier-aborted-before-this-group"
    done
    finish
}
trap on_exit EXIT

# ---------------------------------------------------------------------------
# Group inventory. groups.txt is the only declaration; total_groups is its
# non-comment line count.
# ---------------------------------------------------------------------------
# Line format: <name> <expected-case-count> <kind> <relative-weight>
# kind is `gate` or `scored`; the weight is relative and normalised at scoring
# time, so deleting a group cannot leave the weights failing to sum to 1.0.
# A malformed or missing kind is an authoring fault, not a candidate failure --
# it is rejected below rather than defaulted, because defaulting it either way
# silently mis-prices the step.
GROUP_NAMES=()
declare -A GROUP_WANT=()
declare -A GROUP_KIND=()
declare -A GROUP_WEIGHT=()
manifest_bad=''
while read -r gname gwant gkind gweight _rest; do
    case "$gname" in ''|'#'*) continue ;; esac
    case "$gkind" in
    gate|scored) ;;
    *) manifest_bad="${manifest_bad}${manifest_bad:+; }group $gname has kind '${gkind:-<empty>}', want gate|scored" ;;
    esac
    case "$gweight" in
    ''|*[!0-9.]*) manifest_bad="${manifest_bad}${manifest_bad:+; }group $gname has weight '${gweight:-<empty>}'" ;;
    esac
    GROUP_NAMES+=("$gname")
    GROUP_WANT[$gname]=$gwant
    GROUP_KIND[$gname]=$gkind
    GROUP_WEIGHT[$gname]=$gweight
done < "$A/groups.txt"

# ---------------------------------------------------------------------------
# Asset integrity. A corrupt corpus must fail the groups that consume it rather
# than hand out free points, so it is a prerequisite, not a group.
# ---------------------------------------------------------------------------
( cd "$A" && sha256sum -c SHA256SUMS ) > "$log_dir/assets.log" 2>&1
assets_ok=$?
declared=$(cat "$A/GROUP_COUNT")
# A directory whose name begins with '_' is shared driver source, not a group:
# it is compiled as part of whichever groups copy it, holds no cases, and is
# absent from groups.txt by construction. Counting it here would make every
# step that has one fail this prerequisite and score zero.
ondisk=$(find "$A/groups" -mindepth 1 -maxdepth 1 -type d -not -name '_*' | wc -l)
printf 'assets sha check=%s groups.txt=%s dirs=%s declared=%s\n' \
    "$assets_ok" "${#GROUP_NAMES[@]}" "$ondisk" "$declared" >> "$log_dir/assets.log"
if [ "$assets_ok" -ne 0 ] || [ "$ondisk" != "$declared" ] || [ "${#GROUP_NAMES[@]}" != "$declared" ]; then
    fail_everything "verifier asset corpus failed its integrity check"
fi
for g in "${GROUP_NAMES[@]}"; do
    [ -d "$A/groups/$g" ] || fail_everything "groups.txt names $g, assets/groups/$g is absent"
    if [ ! -f "$A/groups/$g/run.sh" ] && ! ls "$A/groups/$g"/*.go >/dev/null 2>&1; then
        fail_everything "group $g has neither run.sh nor a driver"
    fi
    # groups.txt is generated from the per-group COUNT/KIND/WEIGHT files, which
    # ship beside each driver and are the obvious place an editor reaches for
    # when changing a group's size or tier. Scoring reads only the manifest, so
    # a divergence would otherwise surface as an executed-count failure in one
    # group pointing at the wrong file, or -- worse for KIND/WEIGHT -- not at
    # all. Cross-check them here and treat a disagreement as the authoring
    # fault it is.
    if [ -f "$A/groups/$g/COUNT" ]; then
        _fc=$(tr -d ' \t\n' < "$A/groups/$g/COUNT")
        [ "$_fc" = "${GROUP_WANT[$g]}" ] || fail_everything \
            "group $g: COUNT file says '$_fc', groups.txt says '${GROUP_WANT[$g]}'"
    fi
    if [ -f "$A/groups/$g/KIND" ]; then
        _fk=$(tr -d ' \t\n' < "$A/groups/$g/KIND")
        [ "$_fk" = "${GROUP_KIND[$g]}" ] || fail_everything \
            "group $g: KIND file says '$_fk', groups.txt says '${GROUP_KIND[$g]}'"
    fi
    if [ "${GROUP_KIND[$g]}" = scored ]; then
        if [ -f "$A/groups/$g/WEIGHT" ]; then _fw=$(tr -d ' \t\n' < "$A/groups/$g/WEIGHT"); else _fw=1; fi
        [ "$_fw" = "${GROUP_WEIGHT[$g]}" ] || fail_everything \
            "group $g: WEIGHT file says '$_fw', groups.txt says '${GROUP_WEIGHT[$g]}'"
    elif [ -f "$A/groups/$g/WEIGHT" ]; then
        fail_everything "group $g is kind gate but ships a WEIGHT file; gate groups earn nothing"
    fi
done
if [ "${GROUP_WANT[$GATE_GROUP]:-missing}" = missing ]; then
    fail_everything "groups.txt declares no $GATE_GROUP group"
fi
if [ "${GROUP_KIND[$GATE_GROUP]:-}" != gate ]; then
    fail_everything "$GATE_GROUP is kind '${GROUP_KIND[$GATE_GROUP]:-<none>}', must be gate"
fi
# Tier sanity. Both of these are authoring faults that would silently mis-price
# every candidate: with no gate group nothing can veto, and with no scored
# group the step can never pay out even for a perfect implementation.
if [ -n "$manifest_bad" ]; then
    fail_everything "groups.txt manifest is malformed: $manifest_bad"
fi
_gate_n=0; _scored_n=0
for g in "${GROUP_NAMES[@]}"; do
    if [ "${GROUP_KIND[$g]}" = gate ]; then _gate_n=$((_gate_n + 1)); else _scored_n=$((_scored_n + 1)); fi
done
if [ "$_gate_n" -eq 0 ]; then
    fail_everything "groups.txt declares no gate group: nothing can veto this step"
fi
if [ "$_scored_n" -eq 0 ]; then
    fail_everything "groups.txt declares no scored group: this step can never pay out"
fi

# ---------------------------------------------------------------------------
# Workspace. The build manifest is benchmark-owned (TEST_INTERFACE.md section 2):
# the verifier restores its own pinned copy so a candidate cannot change how it is
# graded, or make the module graph unresolvable offline.
# ---------------------------------------------------------------------------
give_app_to_unprivileged
install -m 0644 -o "$SCORE_UID" -g "$SCORE_GID" "$A/gomod/go.mod" /app/go.mod
install -m 0644 -o "$SCORE_UID" -g "$SCORE_GID" "$A/gomod/go.sum" /app/go.sum

cp -R "$A/verifier/." "$W/ctverify/"
mkdir -p "$W/ctverify/g" "$W/gocache" "$W/tmp"
chown -R "$SCORE_UID:$SCORE_GID" "$W"

# GOPROXY=off is what makes grading offline, and it is also what makes
# -mod=mod safe here: with the proxy disabled, a module the manifest does not
# already pin cannot be resolved at all, let alone resolved to @latest. What
# -mod=mod buys is that the verifier's own ctverify module may complete its
# requirement graph from the module cache baked into this image, instead of
# failing the whole run over a go.sum line. GOTOOLCHAIN=local pins the
# compiler to the one in the image; GOWORK=off stops a stray go.work in the
# candidate's tree from redefining the build.
GOENV=(
    env HOME=/tmp TMPDIR="$W/tmp"
    GOFLAGS=-mod=mod GOPROXY=off GOSUMDB=off GOTOOLCHAIN=local GOWORK=off
    GOCACHE="$W/gocache" GOMODCACHE=/go/pkg/mod
    CT_VERIFY_NONCE="$CT_VERIFY_NONCE"
    CT_ASSETS="$A" CT_BIN="$W/bin" CT_FIXTURES="$A/fixtures"
)

# ---------------------------------------------------------------------------
# Prerequisite, not a group: build the candidate's binaries once so the
# process-level groups have something to run. Best effort - a group that needs
# a binary that is absent fails that group alone, and `build-clean` scores the
# compile itself like any other group.
# ---------------------------------------------------------------------------
(
    cd /app && run_unprivileged "${GOENV[@]}" timeout 1500 go build -o "$W/bin/" ./...
) >> "$log_dir/build.log" 2>&1 || true

# Verifier-side helper programs, if this step ships any. They live in the
# throwaway module and never import the candidate, so they build even when /app
# does not. (There is no backend stub -- see TEST_INTERFACE.md section 3.4.)
for tool in "$A"/verifier/tools/*; do
    [ -d "$tool" ] || continue
    (
        cd "$W/ctverify" && run_unprivileged "${GOENV[@]}" timeout 900 \
            go build -o "$W/bin/vtool-$(basename "$tool")" "./tools/$(basename "$tool")"
    ) >> "$log_dir/module.log" 2>&1 || true
done

# ---------------------------------------------------------------------------
# One group, one compilation unit, one process (TEST_INTERFACE.md section 3.1).
# ---------------------------------------------------------------------------
run_group() {  # run_group <name>
    local name="$1" want="${GROUP_WANT[$1]}" d="$A/groups/$1"
    local logf="$log_dir/case-logs/$name.log" scratch rc executed failed cases summary
    : > "$logf"
    scratch=$(mktemp -d "$W/tmp/g-XXXXXX")
    chown "$SCORE_UID:$SCORE_GID" "$scratch"

    if [ -f "$d/run.sh" ]; then
        (
            cd "$scratch" && run_unprivileged "${GOENV[@]}" CT_GROUP_DIR="$d" \
                CT_SCRATCH="$scratch" timeout 600 bash "$d/run.sh"
        ) >> "$logf" 2>&1
        rc=$?
    else
        mkdir -p "$W/ctverify/g/$name"
        cp "$d"/*.go "$W/ctverify/g/$name/"
        chown -R "$SCORE_UID:$SCORE_GID" "$W/ctverify/g/$name"
        if ! (
            cd "$W/ctverify" && run_unprivileged "${GOENV[@]}" timeout 900 \
                go build -o "$W/bin/$name" "./g/$name"
        ) >> "$logf" 2>&1; then
            record "$name" 1 "driver did not compile"
            rm -rf "$scratch"
            return
        fi
        (
            cd "$scratch" && run_unprivileged "${GOENV[@]}" CT_GROUP_DIR="$d" \
                CT_SCRATCH="$scratch" timeout 600 "$W/bin/$name"
        ) >> "$logf" 2>&1
        rc=$?
    fi
    rm -rf "$scratch"

    # Only nonce-prefixed lines count, so nothing the group drives can print
    # itself a pass.
    summary=$(grep -c "^$CT_VERIFY_NONCE SUMMARY " "$logf")
    cases=$(grep -c "^$CT_VERIFY_NONCE CASE " "$logf")
    executed=$(sed -n "s/^$CT_VERIFY_NONCE SUMMARY executed=\([0-9][0-9]*\) failed=\([0-9][0-9]*\).*/\1/p" "$logf" | tail -1)
    failed=$(sed -n "s/^$CT_VERIFY_NONCE SUMMARY executed=\([0-9][0-9]*\) failed=\([0-9][0-9]*\).*/\2/p" "$logf" | tail -1)
    executed=${executed:-0}; failed=${failed:-0}
    printf 'group %s: rc=%s summaries=%s cases=%s executed=%s want=%s failed=%s\n' \
        "$name" "$rc" "$summary" "$cases" "$executed" "$want" "$failed" >> "$logf"

    # Executed-count assertion: a group that matched no case, or a corpus that
    # lost one, must not report success having measured nothing. A missing or
    # duplicated SUMMARY is a failure too, so a group killed part way through
    # cannot pass, and one that printed two summaries cannot pick the kinder.
    if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
        record "$name" 1 "timed out (rc=$rc) after executed=$executed/$want"
    elif [ "$summary" -ne 1 ]; then
        record "$name" 1 "expected exactly one SUMMARY line, saw $summary"
    elif [ "$cases" -ne "$executed" ]; then
        record "$name" 1 "SUMMARY says executed=$executed but $cases CASE lines were printed"
    elif [ "$executed" -ne "$want" ] || [ "$failed" -ne 0 ]; then
        record "$name" 1 "executed=$executed/$want failed=$failed"
    elif [ "$rc" -ne 0 ]; then
        record "$name" 1 "every case passed but the group exited $rc"
    else
        record "$name" 0 "executed=$executed"
    fi
}

for g in "${GROUP_NAMES[@]}"; do
    case "$recorded" in *",$g,"*) continue ;; esac
    run_group "$g"
done

finish
