#!/usr/bin/env bash

# --- verifier/step binding check -------------------------------------------
# /etc/harbor-step-id names the image; the literal below names this tests/ tree.
# They disagree only when the build context came from another step's cache
# entry, in which case every check below would grade the wrong milestone.
# Reported, not crashed: a non-zero exit makes Harbor skip the remaining steps
# and buries the fault under a second symptom.
__hb_want=04
__hb_got=$(cat /etc/harbor-step-id 2>/dev/null) || __hb_got=MISSING
if [ "$__hb_got" != "$__hb_want" ]; then
    __hb_msg="VERIFIER/STEP MISMATCH: image is step ${__hb_got}, /tests tree is step ${__hb_want}."
    printf '%s\n' "$__hb_msg" >&2
    mkdir -p /logs/verifier 2>/dev/null
    printf '%s\n' "$__hb_msg" > /logs/verifier/step-mismatch.log
    printf '{"reward":0,"verifier_error":"step_mismatch","image_step":"%s","tests_step":"%s"}\n' \
        "$__hb_got" "$__hb_want" > /logs/verifier/reward.json
    exit 0
fi
unset __hb_want __hb_got __hb_msg
# --- end binding check ------------------------------------------------------
# Verifier entry point for step 04 (04-upsert), SQLite 3.24.0.
#
# Generated from misc/sql-database-multiversion/harness/test.sh by
# build-tests.sh. Every step shares this file; what differs between steps is
# assets/groups.txt, assets/cases/ and assets/gate/.
#
# Runs in this step's verifier container on a fresh copy of /app. Nothing
# written here reaches the next step; only /logs/verifier is read back.
#
# Scoring is step-local and ungated: reward.json carries
# `passed_groups / total_groups` for THIS step, and task.toml averages the five
# (multi_step_reward_strategy = "mean"). No step sets min_reward, so nothing
# here can abort the trial. The former gate corpus is now one ordinary scored
# group, named by $GATE_GROUP below.
set -u
source /tests/helpers.sh

# The one scored group whose cases live in assets/gate/ rather than
# assets/cases/<group>/: the former gate, kept as real end-to-end coverage.
# Its expected case count in groups.txt must equal assets/gate/COUNT.
GATE_GROUP=gate-endtoend

STEP=04
RELEASE=3.24.0

log_dir=/tmp/sqlite-verifier
work=/tmp/sqlite-work
rm -rf -- "$log_dir" "$work"
mkdir -p "$log_dir" "$work"

app=/app
export SQLITE3="$app/build/sqlite3"
export SQLITE_LIB="$app/build/libsqlite3.a"
export SQLITE_INC="$app/include"

groups_file=/tests/assets/groups.txt

# ---------------------------------------------------------------------------
# Reporting helpers
# ---------------------------------------------------------------------------
passed=0
total=0
newcap_passed=0
newcap_total=0
inherited_passed=0
inherited_total=0
group_json=''
recorded=','
finished=0
# Diagnostic only: 1 when the end-to-end group ($GATE_GROUP) passed. It is
# reported in reward_details.json, does not scale the reward, and task.toml no
# longer reads a `gate` key at all.
gate_result=0

# A group whose name begins with `inherited-` or `regression-` re-exercises a
# milestone this step INHERITS; every other group covers this step's own new
# capability. Both tallies are reported so any claim about either is checkable.
# $GATE_GROUP walks one end-to-end path through this step's new capability as
# well as through the milestones it inherits, so it is counted with the new
# capability rather than split.
#
# Scoring is step-local and ungated: this step's reward is simply
# `passed_groups / total_groups` over the groups in assets/groups.txt, and
# task.toml averages the five steps. The former gate is one of those groups -
# $GATE_GROUP, whose cases live in assets/gate/ - so failing it now costs
# exactly one group instead of zeroing the milestone, and every other group
# still runs and reports its true outcome here and in reward_details.json. The
# no-op criterion is still met literally: a workspace that implements nothing
# passes no group, so passed_groups is 0.
record() {  # record <group> <0=pass|1=fail> <detail>
    local name="$1" status="$2" detail="${3:-}" bit=0
    total=$((total + 1))
    if [ "$status" -eq 0 ]; then
        passed=$((passed + 1)); bit=1
    fi
    case "$name" in
    inherited-*|regression-*)
        inherited_total=$((inherited_total + 1))
        [ "$bit" -eq 1 ] && inherited_passed=$((inherited_passed + 1))
        ;;
    *)
        newcap_total=$((newcap_total + 1))
        [ "$bit" -eq 1 ] && newcap_passed=$((newcap_passed + 1))
        ;;
    esac
    group_json="${group_json}${group_json:+,}\"$name\":$bit"
    recorded="${recorded}${name},"
    printf '%s %s %s\n' "$([ "$bit" -eq 1 ] && echo PASS || echo FAIL)" "$name" "$detail" \
        >> "$log_dir/groups.txt"
}

write_details() {  # write_details <gate-diagnostic>
    local gate="$1"
    printf '{"step":"%s","release":"%s","gate":%s,"passed_groups":%s,"total_groups":%s,"new_capability":{"passed":%s,"total":%s},"inherited_regression":{"passed":%s,"total":%s},"groups":{%s}}\n' \
        "$STEP" "$RELEASE" "$gate" "$passed" "$total" \
        "$newcap_passed" "$newcap_total" "$inherited_passed" "$inherited_total" \
        "$group_json" \
        > /logs/verifier/reward_details.json
}

finish() {  # finish   (no arguments: the score is step-local and ungated)
    local score
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
    for b in build clean assets; do
        [ -s "$log_dir/$b.log" ] && tail -c 20000 "$log_dir/$b.log" > "/logs/verifier/$b.log"
    done
    # This step's score, and nothing else: the fraction of this step's own
    # groups that passed. There is no gate multiplier - `gate_result` is
    # carried into reward_details.json as a diagnostic only - and no divisor by
    # the number of task steps, because Harbor's
    # multi_step_reward_strategy = "mean" averages the five steps itself.
    score=$(awk -v n="$passed" -v d="$total" \
        'BEGIN { if (d <= 0) printf "0.000000"; else printf "%.6f", n / d }')
    write_details "$gate_result"
    report "$score"
}

# ---------------------------------------------------------------------------
# Every group is declared up front from groups.txt, and every declared group is
# reported exactly once, as a pass or a failure - never skipped. A group whose
# prerequisite is missing is scored 0, and the exit trap below closes out any
# group the run never reached, so a verifier that dies half way through still
# reports the full list rather than a short one.
# ---------------------------------------------------------------------------
declare -a GROUP_NAMES=()
declare -A GROUP_WANT=()
while read -r gname gcount; do
    case "$gname" in ''|\#*) continue ;; esac
    GROUP_NAMES+=("$gname")
    GROUP_WANT["$gname"]=$gcount
done < "$groups_file"

fail_everything() {  # fail_everything <reason>
    : > "$log_dir/groups.txt"
    passed=0; total=0; group_json=''; recorded=','
    newcap_passed=0; newcap_total=0; inherited_passed=0; inherited_total=0
    gate_result=0
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
# Asset integrity. A corrupt case corpus must fail the groups that consume it
# rather than hand out free points, so it is a prerequisite, not a group.
# ---------------------------------------------------------------------------
( cd /tests/assets && sha256sum -c CASE_SHA256SUMS ) > "$log_dir/assets.log" 2>&1
assets_ok=$?
case_total=$(find /tests/assets/cases /tests/assets/gate -type f \
    \( -name '*.sql' -o -name '*.sh' -o -name '*.c' \) | wc -l)
expected_case_total=$(cat /tests/assets/CASE_COUNT)
printf 'assets sha check=%s cases=%s expected=%s\n' \
    "$assets_ok" "$case_total" "$expected_case_total" >> "$log_dir/assets.log"
if [ "$assets_ok" -ne 0 ] || [ "$case_total" != "$expected_case_total" ]; then
    fail_everything "verifier asset corpus failed its integrity check"
fi
# The end-to-end group is declared in groups.txt like any other group, but its
# corpus is assets/gate/, whose size is pinned separately by assets/gate/COUNT.
# The two must agree or that group's executed-count assertion is meaningless.
if [ "${GROUP_WANT[$GATE_GROUP]:-missing}" != "$(cat /tests/assets/gate/COUNT)" ]; then
    fail_everything "groups.txt $GATE_GROUP count disagrees with assets/gate/COUNT"
fi

# ---------------------------------------------------------------------------
# Build the candidate. /app is untrusted candidate data: refuse to follow a
# symlink out of the workspace, and never write anywhere else inside it.
# ---------------------------------------------------------------------------
drop_build() {
    if [ -L "$app/build" ]; then rm -f -- "$app/build"; else rm -rf -- "$app/build"; fi
}
drop_build

# `make clean` must succeed and remove /app/build (TEST_INTERFACE.md section
# 1). It is probed here, against a stand-in build directory and *before* the
# real build, so a clean rule that removes too much cannot damage anything the
# scored groups then need.
clean_ok=1
if [ -f "$app/Makefile" ]; then
    mkdir -p "$app/build/probe.d" && : > "$app/build/probe.f"
    ( cd "$app" && timeout 120 make clean ) > "$log_dir/clean.log" 2>&1
    clean_rc=$?
    if [ "$clean_rc" -eq 0 ] && [ ! -e "$app/build" ]; then clean_ok=0; fi
    printf 'make clean status=%s /app/build present afterwards=%s\n' "$clean_rc" \
        "$([ -e "$app/build" ] && echo yes || echo no)" >> "$log_dir/clean.log"
    drop_build
else
    echo "no /app/Makefile" > "$log_dir/clean.log"
fi

build_ok=1
if [ ! -f "$app/Makefile" ]; then
    echo "no /app/Makefile" > "$log_dir/build.log"
else
    ( cd "$app" && timeout 900 make ) > "$log_dir/build.log" 2>&1
    build_ok=$?
fi
[ -f "$SQLITE3" ] && [ -x "$SQLITE3" ] || build_ok=1
[ -f "$SQLITE_LIB" ] || build_ok=1
[ -f "$SQLITE_INC/sqlite3.h" ] || build_ok=1
{
    printf 'make status=%s\n' "$build_ok"
    ls -l "$app/build" 2>&1 | head -20
} >> "$log_dir/build.log"

# ---------------------------------------------------------------------------
# Case execution
# ---------------------------------------------------------------------------
mkdir -p "$log_dir/case-logs"
source /tests/assets/lib/runner.sh

run_group() {  # run_group <name>
    # The end-to-end group is the former gate; its cases live in assets/gate/
    # rather than under assets/cases/ so that the corpus layout, the integrity
    # manifest and assets/gate/COUNT all stay exactly as they were.
    local name="$1" dir="/tests/assets/cases/$1"
    if [ "$name" = "$GATE_GROUP" ]; then dir=/tests/assets/gate; fi
    local want="${GROUP_WANT[$name]}"
    local logf="$log_dir/case-logs/$name.log"
    : > "$logf"

    if [ "$build_ok" -ne 0 ]; then
        echo "candidate did not build; see build.log" >> "$logf"
        record "$name" 1 "build-failed"
        return
    fi
    if [ ! -d "$dir" ]; then
        echo "missing case directory $dir" >> "$logf"
        record "$name" 1 "no-cases"
        return
    fi

    local executed=0 failed=0 case_path
    while IFS= read -r case_path; do
        executed=$((executed + 1))
        if run_case "$case_path" "$logf"; then :; else failed=$((failed + 1)); fi
    done < <(find "$dir" -maxdepth 1 -type f \( -name '*.sql' -o -name '*.sh' -o -name '*.c' \) \
             | LC_ALL=C sort)

    printf 'group %s: executed=%s want=%s failed=%s\n' \
        "$name" "$executed" "$want" "$failed" >> "$logf"
    # Executed-count assertion: a runner that matched no case, or a corpus that
    # lost one, must not report success having measured nothing.
    if [ "$executed" -ne "$want" ] || [ "$failed" -ne 0 ]; then
        record "$name" 1 "executed=$executed/$want failed=$failed"
    else
        record "$name" 0 "executed=$executed"
        if [ "$name" = "$GATE_GROUP" ]; then gate_result=1; fi
    fi
}

# A group declared in groups.txt with an expected case count of 0 is a harness
# probe rather than a directory of cases: it observes something about the build
# itself, which no case running after the build could observe without damaging
# it. Each probe still records exactly one pass/fail like any other group.
run_probe() {  # run_probe <name>
    local name="$1"
    local logf="$log_dir/case-logs/$name.log"
    : > "$logf"
    # A probe observes the candidate's build, so like every case group it is a
    # failure when there is no working build to observe - otherwise the shipped
    # scaffold would satisfy it before a line of the implementation exists.
    if [ "$build_ok" -ne 0 ]; then
        echo "candidate did not build; see build.log" >> "$logf"
        record "$name" 1 "build-failed"
        return
    fi
    case "$name" in
    build-clean)
        cat "$log_dir/clean.log" >> "$logf" 2>/dev/null || true
        if [ "$clean_ok" -eq 0 ]; then
            record "$name" 0 "make clean removed /app/build"
        else
            record "$name" 1 "make clean did not succeed and remove /app/build"
        fi
        ;;
    *)
        echo "no probe is implemented for group $name" >> "$logf"
        record "$name" 1 "unknown-probe"
        ;;
    esac
}

: > "$log_dir/groups.txt"
for g in "${GROUP_NAMES[@]}"; do
    if [ "${GROUP_WANT[$g]}" -eq 0 ]; then
        run_probe "$g"
    else
        run_group "$g"
    fi
done

# The end-to-end group ran with the rest of them above; nothing is left to do
# but report this step's own pass rate.
finish
