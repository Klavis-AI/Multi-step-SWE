#!/usr/bin/env bash

# --- verifier/step binding check -------------------------------------------
# /etc/harbor-step-id names the image; the literal below names this tests/ tree.
# They disagree only when the build context came from another step's cache
# entry, in which case every check below would grade the wrong milestone.
# Reported, not crashed: a non-zero exit makes Harbor skip the remaining steps
# and buries the fault under a second symptom.
__hb_want=10
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
# Verifier driver for step 10 (srt 1.5.2).
set -u
source /tests/helpers.sh

# ---------------------------------------------------------------------------
# 0. Crash path.
#
# Under `multi_step_reward_strategy = "mean"` a verifier that exits without a
# reward.json costs the run every step after it, so every exit from here
# on writes a complete scorecard and exits 0. `fail_everything` is the
# scorecard for a driver that died before it could measure anything: zero
# groups passed, the declared total intact, and a note saying so.
# ---------------------------------------------------------------------------
reported=0

fail_everything() {
    local reason="$1"
    # gate 0 here means "the gate was never evaluated", which is why it names
    # itself in gate_failures rather than leaving the field empty: a reader
    # must be able to tell a vetoed candidate from a dead verifier.
    printf '{"step":%s,"reward":0.000000,"gate":0,"score":0.000000,"gate_groups":{},"gate_failures":{"verifier":"%s"},"scored_groups":{},"scored_passed":0,"scored_total":%s,"weight_passed":0,"weight_total":%s,"candidate_build":0,"harness_build":0,"fixture_ok":0,"budget_exhausted":0,"manifest_ok":0,"group_count_ok":0,"build_recipe":"none","build_note":"%s"}\n' \
        "${step_num:-0}" "$reason" "${groups:-0}" "${weight_total:-0}" "$reason" \
        > /logs/verifier/reward_details.json
    printf '{"reward":0.000000}\n' > /logs/verifier/reward.json
}

on_exit() {
    local status=$?
    if [ "${reported:-0}" -ne 1 ]; then
        fail_everything "the verifier driver exited with status $status before it reported: nothing was measured, so this is an unattributable verifier failure rather than a capability result"
    fi
    exit 0
}
trap on_exit EXIT

step_num=10
log_dir=/tmp/srt-verifier-10
cand_build=/tmp/srt-cand-build-10
cand_root=/tmp/srt-cand-root-10
drv_src=/tests/assets/drv
drv_build=/tmp/srt-drv-build-10
groups_manifest=/tests/assets/groups.txt
mkdir -p "$log_dir"

# ---------------------------------------------------------------------------
# 1. Fixture integrity.
#
# The fixtures here are the driver sources and the group manifest, not a
# downloaded corpus: every packet, key and schedule a case uses is a literal
# inside a `fam_*.cpp`. Verifying the tree still catches the one failure mode
# that would otherwise be silent -- a build context assembled from a stale or
# partial cache entry, which would run a different step's cases under this
# step's name. A mismatch never short-circuits the run; it annotates it.
# ---------------------------------------------------------------------------
expected_assets_sha=7f1edb16c0856b054f88cd6d1a9aa9422bcffe97b6b442d45b58a4ba1aad7882
expected_assets_files=8

actual_assets_sha=$(cd /tests/assets && find . -type f -print0 | LC_ALL=C sort -z \
    | xargs -0 sha256sum | sha256sum | awk '{print $1}')
actual_assets_files=$(find /tests/assets -type f | wc -l)

fixture_ok=1
[ "$actual_assets_sha" = "$expected_assets_sha" ] || fixture_ok=0
[ "$actual_assets_files" -eq "$expected_assets_files" ] || fixture_ok=0

# ---------------------------------------------------------------------------
# 2. The time budget, and why these numbers.
#
# `task.toml` gives this step `[steps.verifier] timeout_sec = 2700`. If the
# verifier is killed by that timeout it never writes reward.json, and the run is
# unattributable rather than a zero -- so the work inside must provably fit,
# whatever the candidate does. Four bounds make it fit:
#
#   verify the driver tree                       <1 s  (a few files, one sha pass)
#   CANDIDATE_BUILD_TIMEOUT  one cmake configure + `make -j4` + `make install`
#                            of whatever is in /app              <= 900 s
#   DRIVER_BUILD_TIMEOUT     compile this step's drivers against
#                            that installation                   <= 300 s
#   CASE_BUDGET_TOTAL        every gate check and scored group, together
#                                                                <= 1200 s
#   write reward.json / reward_details.json      <1 s
#                                                     total <= 2405 s
#
# PER_CASE_MAX is deliberately far above any real case and far below the
# aggregate: one hanging case costs its own group and at most 150 s of the
# shared pool, so a step with many hanging cases degrades group by group instead
# of taking the run down. SRT_CASE_TIMEOUT bounds each *case* inside a group the
# same way, from inside the driver, so a single wedged handshake cannot spend a
# whole group's share either.
CANDIDATE_BUILD_TIMEOUT=900
DRIVER_BUILD_TIMEOUT=300
CASE_BUDGET_TOTAL=1200
PER_CASE_MAX=150
SRT_CASE_TIMEOUT=60
export SRT_CASE_TIMEOUT

case_deadline=0
budget_exhausted=0

# ---------------------------------------------------------------------------
# 3. Build the candidate, then the drivers.
#
# The candidate is configured and built from the restored /app artifact into a
# scratch tree and installed into a staging prefix; nothing the candidate may
# have left behind in its own build directory is used, and the artifact excludes
# it anyway. The drivers are a separate CMake project owned by the verifier:
# they include <srt/srt.h> from the staging prefix and link the library found
# there, so they exercise the installed public interface and not the candidate's
# internals.
#
# The two outcomes are recorded separately from the group results. One compile
# error would otherwise be reported as N unrelated capability gaps.
#
# Two recipes, tried in order, and the reason is the gate.
#
# Proof of life is a GATE group, and nothing in the gate may be satisfiable
# only by the new capability. Where a milestone changes the BUILD RECIPE, this
# step's own `cmake` line is exactly that: a candidate who has not done the
# migration leaves a tree the new recipe cannot configure at all -- at step
# 10, for example, `-DUSE_ENCLIB=openssl-evp` fails at v1.5.0 on a missing
# `haicrypt/filelist-openssl-evp.maf`. If that ended the
# build, the step would report 0 with its gate never evaluated, which reads
# identically to a candidate that did nothing.
#
# So the migration leaves the gate: proof of life asks only that SOME recipe
# from this chain builds the tree. The new recipe is still priced -- by the
# scored groups that need what it enables, which fail without it. The fallback
# is the previous milestone's recipe verbatim, the one recipe the previous
# tree is known to build with.
#
# `build_recipe` records which one ran, so a reader can tell a candidate that
# built under this step's recipe from one that only built under the last.
# ---------------------------------------------------------------------------
candidate_build=0
build_recipe=none
cand_deadline=$(( $(date +%s) + CANDIDATE_BUILD_TIMEOUT ))

try_candidate_build() {
    local flags="$1" left
    left=$(( cand_deadline - $(date +%s) ))
    [ "$left" -gt 5 ] || return 1
    timeout "$left" bash -c '
        set -e
        rm -rf -- "$1" "$2"
        mkdir -p -- "$1" "$2"
        cmake -S /app -B "$1" -DCMAKE_BUILD_TYPE=Release -DENABLE_APPS=OFF \
              -DCMAKE_INSTALL_PREFIX=/usr/local $3
        make -C "$1" -j4
        make -C "$1" install DESTDIR="$2"
    ' _ "$cand_build" "$cand_root" "$flags" >>"$log_dir/candidate-build.log" 2>&1
}

printf '=== recipe: step 10 (-DENABLE_BONDING=ON -DENABLE_AEAD_API_PREVIEW=ON -DUSE_ENCLIB=openssl-evp)\n' >"$log_dir/candidate-build.log"
if try_candidate_build "-DENABLE_BONDING=ON -DENABLE_AEAD_API_PREVIEW=ON -DUSE_ENCLIB=openssl-evp"; then
    candidate_build=1
    build_recipe=step
else
    printf '\n=== the step recipe did not build; retrying with the previous milestone'"'"'s recipe (-DENABLE_BONDING=ON)\n' \
        >>"$log_dir/candidate-build.log"
    if try_candidate_build "-DENABLE_BONDING=ON"; then
        candidate_build=1
        build_recipe=fallback
    fi
fi

# The driver families are configured once and then built ONE TARGET AT A TIME,
# and a target that fails to compile is not allowed to take the others with it.
#
# This matters more than it looks. A family is one translation unit holding
# several groups, and a group that asserts a new socket option names an
# enumerator that a candidate who has not implemented that option does not have
# in its header. With a single `cmake --build`, that one missing enumerator
# fails the whole driver build, and every group of every family scores zero --
# including groups the candidate genuinely satisfies, in families that compiled
# perfectly well. The step then measures "did you miss anything" instead of
# "how much did you build", which is not what score = passed/total claims to
# be. Building per target confines a missing identifier to its own family.
#
# The family list comes from the groups manifest rather than from a glob, so it
# is the same list the scoring loop uses and the two cannot drift.
#
# DRIVER_BUILD_TIMEOUT is the budget for configuring *and* building, not for
# each separately: the step's total is sized as CAND_BUILD + DRV_BUILD +
# CASE_TOTAL + 5 <= TIMEOUT_SEC, so a configure that spends the whole budget
# must leave the family loop with nothing, not with a fresh one.
drv_start=$(date +%s)
harness_configured=0
if [ "$candidate_build" -eq 1 ] \
   && timeout "$DRIVER_BUILD_TIMEOUT" bash -c '
        set -e
        rm -rf -- "$2"
        cmake -S "$1" -B "$2" -DCANDIDATE_PREFIX="$3" -DDRIVER_DEFS="-DENABLE_AEAD_API_PREVIEW"
    ' _ "$drv_src" "$drv_build" "$cand_root/usr/local" >"$log_dir/driver-build.log" 2>&1; then
    harness_configured=1
fi

# One shared deadline across the configure and all family builds, so neither a
# slow configure nor N families can each spend DRIVER_BUILD_TIMEOUT and blow the
# step's total. A family that runs out of time is indistinguishable from one
# that failed to compile, which is the right outcome: its binary does not exist
# and its groups score zero.
harness_build=0
harness_families_ok=0
harness_families_bad=""
if [ "$harness_configured" -eq 1 ]; then
    drv_deadline=$(( drv_start + DRIVER_BUILD_TIMEOUT ))
    for fam in $(awk 'NF && $1 !~ /^#/ { print $1 }' "$groups_manifest" | sort -u); do
        fam_left=$(( drv_deadline - $(date +%s) ))
        # The manifest names families bare (`api`); the CMake target and the
        # binary are `fam_api`, the same spelling run_group executes below.
        if [ "$fam_left" -gt 2 ] \
           && timeout "$fam_left" cmake --build "$drv_build" --target "fam_$fam" -j4 \
                >>"$log_dir/driver-build.log" 2>&1 \
           && [ -x "$drv_build/fam_$fam" ]; then
            harness_families_ok=$(( harness_families_ok + 1 ))
        else
            harness_families_bad="$harness_families_bad $fam"
            printf '\n*** family %s did not build ***\n' "$fam" >>"$log_dir/driver-build.log"
        fi
    done
    # `harness_build` records that the verifier got far enough to run
    # something. It is 1 when at least one family exists, so a total driver
    # failure is still reported as a build failure rather than as N
    # capability gaps.
    [ "$harness_families_ok" -gt 0 ] && harness_build=1
fi

# A build that failed has to be diagnosable from the job directory alone.
# $log_dir is inside the container and is thrown away with it, so the tail of
# whichever build failed is copied into /logs/verifier, which Harbor collects.
# The tail rather than the whole log: a CMake error names its cause in the last
# few lines.
if [ "$candidate_build" -ne 1 ]; then
    {
        printf 'candidate build FAILED (last 200 lines of %s)\n' "$log_dir/candidate-build.log"
        tail -n 200 "$log_dir/candidate-build.log" 2>/dev/null || printf '(no log was produced)\n'
    } > /logs/verifier/candidate-build.tail.log 2>&1
    tail -n 40 "$log_dir/candidate-build.log" >&2 2>/dev/null
elif [ "$harness_build" -ne 1 ]; then
    {
        printf 'driver build FAILED (last 200 lines of %s)\n' "$log_dir/driver-build.log"
        tail -n 200 "$log_dir/driver-build.log" 2>/dev/null || printf '(no log was produced)\n'
    } > /logs/verifier/driver-build.tail.log 2>&1
    tail -n 40 "$log_dir/driver-build.log" >&2 2>/dev/null
fi

# The case pool starts here, so a slow build cannot eat it.
case_deadline=$(( $(date +%s) + CASE_BUDGET_TOTAL ))

run_group() {
    # run_group <family> <group>
    #
    # Runs exactly the cases registered under <group> in driver fam_<family>,
    # bounded by the smaller of PER_CASE_MAX and what is left of
    # CASE_BUDGET_TOTAL, so the groups can never overrun that total however many
    # of them hang. A killed group exits non-zero and scores 0, the same outcome
    # as any other failure; once the pool is spent the remaining groups are
    # failed without running and the reason is recorded, which is still a zero
    # rather than a lost run.
    #
    # The final assertion is the guard against a vacuous pass: the driver prints
    # `EXECUTED <n> FAILED <m>`, and <n> must equal the count this group
    # declares in assets/groups.txt. A `--group` filter that matched nothing
    # exits non-zero and prints `EXECUTED 0`, and a group that silently lost a
    # case fails here instead of scoring full marks for less work.
    # Every failure path also writes one plain-English line to
    # $log_dir/<family>.<group>.reason. A gate failure zeroes the step outright,
    # so reward_details.json has to be able to name the case that vetoed and
    # say why; a step reporting 0 without that is indistinguishable from a
    # candidate that did nothing.
    local family="$1" group="$2"
    local want remaining limit status log reason executed t0 t1
    log="$log_dir/$family.$group.log"
    reason="$log_dir/$family.$group.reason"
    : > "$reason"
    # Per family, not global: a sibling family failing to compile must not fail
    # this group, which is the whole point of building the targets separately.
    if [ ! -x "$drv_build/fam_$family" ]; then
        printf 'the verifier driver for family %s did not compile or link against the installed prefix, so this group could not run; a public identifier it names is missing from the installed <srt/srt.h> or is not exported by the installed libsrt\n' \
            "$family" | tee "$reason" >"$log"
        return 1
    fi
    want=$(awk -v f="$family" -v g="$group" '$1 == f && $2 == g { print $3 }' "$groups_manifest")
    if [ -z "${want:-}" ]; then
        printf 'group %s of family %s is not declared in assets/groups.txt\n' \
            "$group" "$family" | tee "$reason" >"$log"
        return 1
    fi
    remaining=$(( case_deadline - $(date +%s) ))
    if [ "$remaining" -le 5 ]; then
        budget_exhausted=1
        printf 'the aggregate case budget of %s s was exhausted before this group ran\n' \
            "$CASE_BUDGET_TOTAL" | tee "$reason" >"$log"
        return 1
    fi
    limit=$PER_CASE_MAX
    [ "$remaining" -lt "$limit" ] && limit=$remaining
    t0=$(date +%s)
    timeout "$limit" "$drv_build/fam_$family" --group "$group" >"$log" 2>&1
    status=$?
    t1=$(date +%s)
    # Elapsed is recorded for every group, pass or fail. When a case dies on a
    # deadline the question is always "was the case slow or was the host", and
    # a log carrying neither number cannot answer it. The line goes after the
    # driver output so the exact-match EXECUTED check below is unaffected, and
    # it deliberately contains no " FAIL " so it cannot be picked up as a
    # case verdict by the reason extractor.
    printf 'GROUP %s.%s elapsed %ss of a %ss limit, driver exit %s\n' \
        "$family" "$group" "$(( t1 - t0 ))" "$limit" "$status" >>"$log"
    if [ "$status" -ne 0 ]; then
        # A failing case prints `CASE <group>.<name> FAIL <message>`, which is
        # the most specific thing there is to say. Falling back matters too: a
        # case killed by its own alarm, or a driver killed at the group limit,
        # prints no FAIL line at all.
        if ! grep -m1 -- ' FAIL ' "$log" > "$reason" 2>/dev/null; then
            if [ "$status" -ge 124 ]; then
                printf 'the driver was killed at this group %s s limit (exit %s) without reporting a case verdict\n' \
                    "$limit" "$status" > "$reason"
            else
                printf 'the driver exited %s without reporting a case verdict\n' \
                    "$status" > "$reason"
            fi
        fi
        return "$status"
    fi
    # The guard against a vacuous pass: the driver prints
    # `EXECUTED <n> FAILED <m>`, and <n> must equal the count this group
    # declares in assets/groups.txt. A `--group` filter that matched nothing
    # exits non-zero and prints `EXECUTED 0`, and a group that silently lost a
    # case fails here instead of scoring full marks for less work.
    if ! grep -qx "EXECUTED $want FAILED 0" "$log"; then
        executed=$(grep -m1 '^EXECUTED ' "$log" 2>/dev/null) || executed='no EXECUTED line'
        printf 'this group declares %s cases and the driver reported "%s", so it did not run the work it is scored for\n' \
            "$want" "$executed" > "$reason"
        return 1
    fi
    return 0
}

manifest_field() {
    # manifest_field <family> <group> <column>. Column 4 is the kind, 5 the
    # weight. assets/groups.txt is the manifest of record: the gate_group and
    # scored_group calls below must agree with its rows, so a disagreement is
    # a verifier defect and is reported as one rather than silently graded.
    awk -v f="$1" -v g="$2" -v c="$3" '$1 == f && $2 == g { print $c }' "$groups_manifest"
}

manifest_ok=1
check_manifest() {
    # check_manifest <name> <family> <group> <kind> <weight>
    local got_kind got_weight
    got_kind=$(manifest_field "$2" "$3" 4)
    got_weight=$(manifest_field "$2" "$3" 5)
    if [ "${got_kind:-}" != "$4" ] || [ "${got_weight:-}" != "$5" ]; then
        manifest_ok=0
        printf 'group %s (%s/%s) is graded as kind=%s weight=%s but assets/groups.txt declares kind=%s weight=%s\n' \
            "$1" "$2" "$3" "$4" "$5" "${got_kind:-none}" "${got_weight:-none}" \
            >> "$log_dir/manifest.txt"
    fi
}

json_escape() {
    # Truncate first, then escape, so a cut can never land inside an escape.
    #
    # 700 characters. A gate failure zeroes the step, so this string is the
    # whole of "and why" -- and the first couple of hundred characters of a
    # CASE line are spent on the group, the case name, the file:line and the
    # restatement of what the relay did, with the measured value and the bound
    # it missed after them. 700 clears the longest ST_REQUIRE_MSG in the package
    # with room to spare and is still far short of anything that could bloat
    # reward_details.json.
    printf '%s' "$1" | tr -d '\000-\037' | cut -c1-700 \
        | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'
}

group_reason() {
    # group_reason <family> <group>
    local f="$log_dir/$1.$2.reason" line=''
    [ -f "$f" ] && line=$(head -n 1 "$f")
    [ -n "${line:-}" ] || line='the group failed without recording a reason, which is itself a verifier defect'
    json_escape "$line"
}

keep_log() {
    # keep_log <name> <family> <group>. $log_dir is inside the container and
    # dies with it, so the tail of each failing group is copied into
    # /logs/verifier, which Harbor collects. Failures only: a green ladder
    # should leave nothing behind, and a passing group's log says nothing a
    # scorecard does not.
    local src="$log_dir/$2.$3.log"
    {
        printf 'group %s FAILED (last 120 lines of %s)\n' "$1" "$src"
        tail -n 120 "$src" 2>/dev/null || printf '(no log was produced)\n'
    } > "/logs/verifier/group-$1.log" 2>&1
}

# ---------------------------------------------------------------------------
# 4. The gate.
#
#     reward = gate x score
#
# The gate is binary and earns nothing. It answers one question -- is the work
# the previous milestones already signed off still intact? -- and it can only
# veto. It is proof of life (the candidate's project configured, built and
# installed, and this step's own driver tree is the one that ran) plus one
# deterministic end-to-end path through each milestone this step carries
# forward. g00_endtoend is a gate group at the steps where it passes at the
# previous release's prefix and a scored group at the steps where it does
# not. Either way a candidate who has not implemented this milestone scores
# zero rather than being vetoed for it -- a group that already passed before
# this milestone cannot veto them for failing to deliver it.
#
# Every gate group runs even after an earlier one has failed, because the
# scorecard has to name every group that vetoed rather than the first.
# ---------------------------------------------------------------------------
gate_ok=1
gate_log=$log_dir/gate.txt
gate_failures=''
: > "$gate_log"
: > "$log_dir/manifest.txt"

gate_fail() {
    # gate_fail <name> <json-escaped reason>
    gate_ok=0
    printf 'FAIL %s\n' "$1" >> "$gate_log"
    gate_failures="${gate_failures}${gate_failures:+,}\"$1\":\"$2\""
}

gate_group() {
    # gate_group <name> <family> <group>
    local name="$1" family="$2" group="$3"
    check_manifest "$name" "$family" "$group" gate 0
    if run_group "$family" "$group"; then
        printf 'PASS %s\n' "$name" >> "$gate_log"
    else
        gate_fail "$name" "$(group_reason "$family" "$group")"
        keep_log "$name" "$family" "$group"
    fi
}

# Proof of life. Not a driver group: these two are the frame's own findings,
# and without either of them nothing below measured anything at all.
if [ "$candidate_build" -eq 1 ] && [ "$fixture_ok" -eq 1 ]; then
    printf 'PASS proof-of-life\n' >> "$gate_log"
elif [ "$candidate_build" -ne 1 ]; then
    gate_fail proof-of-life "the project in /app did not configure, build and install cleanly with the documented commands, so nothing below could run"
else
    gate_fail proof-of-life "the verifier driver source tree failed its integrity check, so the cases that ran are not provably this step's cases"
fi

gate_group transfer live g10_transfer
gate_group recovery live g11_recovery
gate_group encryption live g12_encryption
gate_group endtoend live g00_endtoend

# ---------------------------------------------------------------------------
# 5. The scored groups.
#
# This step's new capability and its peripheral breadth. Each group is one
# coherent behaviour and is invoked separately, so a failure localises; every
# group runs even after another has failed, and no group can stop the step.
# `score` is the weighted pass fraction over these groups alone -- the
# denominator is the declared constant below, so a group that never ran costs
# its weight instead of quietly shrinking the total.
# ---------------------------------------------------------------------------
weight_passed=0
weight_total=18
weight_seen=0
passed=0
groups=9
group_log=$log_dir/groups.txt
group_details=''
: > "$group_log"

scored_group() {
    # scored_group <name> <weight> <family> <group>
    local name="$1" weight="$2" family="$3" group="$4" bit=0
    check_manifest "$name" "$family" "$group" scored "$weight"
    weight_seen=$((weight_seen + weight))
    if run_group "$family" "$group"; then
        bit=1
        passed=$((passed + 1))
        weight_passed=$((weight_passed + weight))
        printf 'PASS %s\n' "$name" >> "$group_log"
    else
        printf 'FAIL %s\n' "$name" >> "$group_log"
        keep_log "$name" "$family" "$group"
    fi
    group_details="${group_details}${group_details:+,}\"$name\":{\"passed\":$bit,\"weight\":$weight}"
}

scored_group cryptomode 2 crypto g01_cryptomode
scored_group gcm-session 2 crypto g02_gcm_session
scored_group badsecret 2 crypto g03_badsecret
scored_group argcheck 2 connect g04_argcheck
scored_group muxer-reuse 2 connect g05_muxer_reuse
scored_group reject-hook 2 connect g06_reject_hook
scored_group onepacket 2 live g07_onepacket
scored_group rcvloss-bytes 2 live g08_rcvloss_bytes
scored_group undecrypted 2 live g09_undecrypted

# ---------------------------------------------------------------------------
# 6. Report.
#
#     reward = gate x score
#
# One failing gate group zeroes the step however good the new work is, which
# is why reward_details.json carries `gate_failures`: a step reporting 0 has to
# say whether it was vetoed and by what, or it is indistinguishable from a
# candidate that did nothing.
# ---------------------------------------------------------------------------
recorded_groups=$(grep -c '[^[:space:]]' "$group_log" 2>/dev/null || echo 0)
group_count_ok=1
if [ "$recorded_groups" -ne "$groups" ] || [ "$weight_seen" -ne "$weight_total" ]; then
    group_count_ok=0
    # Only ever upward. A group list that grew a duplicate entry must not be
    # able to score more than the declared total it was weighed against.
    [ "$weight_seen" -gt "$weight_total" ] && weight_total=$weight_seen
fi

score=$(awk -v n="$weight_passed" -v d="$weight_total" \
    'BEGIN { if (d <= 0) { printf "0.000000"; exit } printf "%.6f", n / d }')
if [ "$gate_ok" -eq 1 ]; then
    reward="$score"
else
    reward=0.000000
fi

gate_details=''
while read -r status name; do
    [ -n "${name:-}" ] || continue
    if [ "$status" = PASS ]; then bit=1; else bit=0; fi
    gate_details="${gate_details}${gate_details:+,}\"$name\":$bit"
done < "$gate_log"

# `candidate_build` and `harness_build` separate "the library did not build"
# and "the drivers did not link against it" from "these N capabilities are
# missing"; without them one build error reads as every group failing
# independently, which is how a build error masquerades as a capability report.
build_note=ok
[ "$candidate_build" -eq 1 ] || build_note="the project in /app did not configure, build and install cleanly with the documented commands, so no group could run: every group below failed at build time rather than at run time, and this is a build failure report, not a capability report"
# Three distinct outcomes, and the note has to tell them apart, because
# "nothing compiled" and "one family of four compiled" are very different
# reports for whoever reads reward_details.json.
if [ "$candidate_build" -ne 1 ]; then
    :
elif [ "$harness_build" -ne 1 ]; then
    build_note="the project in /app built and installed, but none of this step's verifier driver families compiled or linked against the installed prefix: a public header or exported symbol the documented interface requires is missing or has a different shape. Every group below failed at build time rather than at run time"
elif [ -n "$harness_families_bad" ]; then
    build_note="the project in /app built and installed, and some verifier driver families compiled while these did not:$harness_families_bad. Only the groups belonging to those families were failed for that reason; every other group below ran"
fi
if [ "$build_recipe" = fallback ]; then
    build_note="$build_note; the tree in /app would not configure or build under this step's own cmake recipe (-DENABLE_BONDING=ON -DENABLE_AEAD_API_PREVIEW=ON -DUSE_ENCLIB=openssl-evp) and was built under the previous milestone's instead (-DENABLE_BONDING=ON). Proof of life is satisfied either way -- it is a gate group and may not turn on this step's new work -- but every scored group that needs what the step recipe enables will have failed"
fi
[ "$fixture_ok" -eq 1 ] || build_note="$build_note; the driver source tree failed its integrity check, so the cases that ran may not be this step's cases: treat the results below as unattributable rather than as capability gaps"
[ "$budget_exhausted" -eq 0 ] || build_note="$build_note; the aggregate case budget was exhausted, so the groups after that point were failed without running - they are unattributable, not measured capability gaps"
[ "$group_count_ok" -eq 1 ] || build_note="$build_note; this driver's scored group list and its declared weight total disagree, which is a verifier defect - the larger of the two was used as the denominator so the discrepancy cannot inflate the score"
[ "$manifest_ok" -eq 1 ] || build_note="$build_note; a group's kind or weight disagrees with assets/groups.txt ($(tr '\n' ' ' < "$log_dir/manifest.txt" | tr -d '"')), which is a generator defect: the kind used to grade it is the one in this script"
if [ "$gate_ok" -ne 1 ]; then
    build_note="$build_note; THE GATE FAILED, so this step scores 0 whatever the scored groups below did - see gate_failures for the inherited behaviour that broke. The scored fraction it would otherwise have earned is in score"
fi

write_scorecard() {
    # write_scorecard <reward> <score>
    printf '{"step":%d,"reward":%s,"gate":%d,"score":%s,"gate_groups":{%s},"gate_failures":{%s},"scored_groups":{%s},"scored_passed":%d,"scored_total":%d,"weight_passed":%d,"weight_total":%d,"candidate_build":%d,"harness_build":%d,"fixture_ok":%d,"budget_exhausted":%d,"manifest_ok":%d,"group_count_ok":%d,"build_recipe":"%s","build_note":"%s"}\n' \
        "$step_num" "$1" "$gate_ok" "$2" \
        "$gate_details" "$gate_failures" "$group_details" \
        "$passed" "$groups" "$weight_passed" "$weight_total" \
        "$candidate_build" "$harness_build" "$fixture_ok" \
        "$budget_exhausted" "$manifest_ok" "$group_count_ok" \
        "$build_recipe" "$build_note" > /logs/verifier/reward_details.json
}

write_scorecard "$reward" "$score"

reported=1
report "$reward"
exit 0
