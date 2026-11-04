#!/usr/bin/env bash
# Verifier for milestone 08 of seedcalc.
#
#   gate   in {0,1}   every gate group passes completely, or the step is zero
#   score  in [0,1]   weighted pass fraction over the SCORED groups only
#   reward = gate x score
#
# Gate groups earn nothing. They are proof of life -- the cumulative library
# still builds -- plus the inherited end-to-end paths that this milestone does
# NOT change. Their job is to veto: a candidate that broke a contract an earlier
# milestone established scores zero here no matter how much new work it did.
# Their weight column is ignored, so a gate group can never hand a do-nothing
# submission free credit.
#
# Scored groups are this milestone's new capability plus the peripheral breadth
# around it. Their weights are normalised among themselves, so `score` is a
# clean weighted pass fraction and a candidate that changes nothing scores
# exactly 0 -- gate intact, nothing earned.
#
# Scores THIS milestone only: Harbor averages the per-step rewards and never
# re-runs an earlier step's verifier, so this step's own inherited-regression
# obligation is priced inside this number rather than inferred from the steps
# before it.
#
# Three properties this file exists to guarantee:
#
#   * Every group runs even after another fails, and each group is its own Go
#     package under ooxmlverify/. A missing candidate symbol is a compile error
#     in exactly one group instead of a compile error that zeroes the whole step.
#   * Every group carries an executed-count assertion. `go test` exits 0 when a
#     package contains no test that matches, and a package that fails to build
#     prints to stderr and reports no cases at all -- so a silently-empty group
#     would otherwise read as a clean pass. Counting `--- PASS/FAIL/SKIP` lines
#     and requiring the documented number turns all three into a zero.
#     `-v` is load-bearing for this: without it `go test` prints a `--- PASS:`
#     line for nothing, only `--- FAIL:` for failures, so a group in which every
#     case passed would count zero executed cases and score zero.
#   * A gate failure is never silent. reward_details.json names every gate group
#     that vetoed and why, so a zero can be told apart from a zero.
set -u

# A missing or unreadable helpers.sh is the one fault the crash guard below
# cannot cover, because the guard lives in it. Handle that case inline.
source /tests/helpers.sh || {
    mkdir -p /logs/verifier 2>/dev/null || true
    printf '{"reward":0,"verifier_error":"helpers_unreadable"}\n' > /logs/verifier/reward.json
    exit 0
}

# From here on, any fatal exit that never reaches report() scores a diagnosable
# 0 rather than leaving /logs/verifier empty.
install_crash_guard

# Before hardening, and before anything else: this runs no candidate code, and
# a mis-bound image must bail rather than grade the wrong milestone.
require_step 08

harden_reward_dir

STEP=08
GROUP_TIMEOUT=240
BUILD_TIMEOUT=600
WORK=/work
APP=$WORK/app
LOGS=/logs/verifier
mkdir -p "$WORK" "$LOGS"

# ---------------------------------------------------------------- staging ---
# The restored /app is untrusted candidate data. Everything runs on a copy
# outside /app, and any candidate-owned directory named like the driver is
# removed before the candidate's own build, not after it.
cp -a /app "$APP" 2>/dev/null || echo "no /app to grade" >&2
mkdir -p "$APP"
rm -rf "$APP/ooxmlverify"
if [ -d /tests/assets ]; then cp -a /tests/assets "$WORK/assets"; else mkdir -p "$WORK/assets"; fi

# Seed an unprivileged build cache from the image's read-only warm copy. Saves
# a full standard-library rebuild in every scored group.
cp -a /opt/gocache /tmp/gocache 2>/dev/null || mkdir -p /tmp/gocache
mkdir -p /tmp/gomod /tmp/gopath

GOENV=(HOME=/tmp TMPDIR=/tmp GOCACHE=/tmp/gocache GOMODCACHE=/tmp/gomod
       GOPATH=/tmp/gopath GOTOOLCHAIN=local GOPROXY=off GOFLAGS=-buildvcs=false
       CGO_ENABLED=0 SEEDCALC_ASSETS=$WORK/assets)

go_run() {   # go_run <timeout-seconds> <command...>
    local t=$1; shift
    run_unprivileged env "${GOENV[@]}" GOFLAGS="-mod=$BUILD_MODE -buildvcs=false" \
        timeout "$t" sh -c 'cd "$1" || exit 97; shift; exec "$@"' sh "$APP" "$@"
}

chown_work() { chown -R "$SCORE_UID:$SCORE_GID" "$WORK" /tmp/gocache /tmp/gomod /tmp/gopath 2>/dev/null || true; }

# -------------------------------------------------------- dependency set ---
# The dependency set is one pinned union for the whole ladder, seeded into the
# workspace at step 01 and never changed by a milestone. /tests/assets/pin
# carries the same go.mod, go.sum and vendor/ tree, so a candidate that deleted
# or corrupted vendor/ still gets built rather than scoring zero on a
# bookkeeping accident. The pinned copy is a FALLBACK: the candidate's own
# manifest is tried first and is never mixed with the pinned one.
PIN=/tests/assets/pin
PIN_RESTORED=0
restore_pin() {
    [ -f "$PIN/vendor.tar.gz" ] || return 1
    ( cd "$PIN" && sha256sum -c SHA256SUMS ) >/dev/null 2>&1 || return 1
    rm -rf "$APP/vendor"
    tar -xzf "$PIN/vendor.tar.gz" -C "$APP" || return 1
    cp -f "$PIN/go.mod" "$APP/go.mod"
    cp -f "$PIN/go.sum" "$APP/go.sum"
    PIN_RESTORED=1
    chown_work
    return 0
}

if [ ! -f "$APP/go.mod" ] || [ ! -d "$APP/vendor" ]; then restore_pin || true; fi
BUILD_MODE=vendor
[ -d "$APP/vendor" ] || BUILD_MODE=mod

chown_work

# ------------------------------------------------------------ build group ---
# Proof of life at the documented boundary: the cumulative library builds as a
# Go package. This runs BEFORE the driver is planted, so a driver that cannot
# compile against this candidate can never be mistaken for a broken candidate.
#
# `go build ./...` alone is not that proof. With no Go files anywhere, `./...`
# matches no packages and `go build` exits 0 -- so an empty workspace passed
# the gate and collected its weight for free. The gate therefore also requires
# that the root package, the one the whole task is about, exists and is
# listable. `go list .` fails with "no Go files in /app" on an empty tree and
# with the compiler's own error on a broken one.
BUILD_LOG=$WORK/build.log
build_attempt() {
    go_run "$BUILD_TIMEOUT" sh -c 'go build ./... && go list . > /dev/null' \
        >"$BUILD_LOG" 2>&1
}

if build_attempt; then
    BUILD_OK=1
elif [ "$PIN_RESTORED" = 0 ] && restore_pin; then
    BUILD_MODE=vendor
    build_attempt && BUILD_OK=1 || BUILD_OK=0
else
    BUILD_OK=0
fi
cp "$BUILD_LOG" "$LOGS/build.log" 2>/dev/null || true

# ----------------------------------------------------------- driver plant ---
# Planted after the candidate's own build. The drivers import the library by
# its module path, which is read from the candidate's go.mod rather than
# assumed: a candidate that renamed the module is graded on its code, not
# zeroed on its import line.
MODPATH=$(awk '$1=="module"{print $2; exit}' "$APP/go.mod" 2>/dev/null)
[ -n "$MODPATH" ] || MODPATH=github.com/harbor-bench/seedcalc
cp -a /tests/driver "$APP/ooxmlverify"
# Reject a symlinked or otherwise hostile staging target before running anything.
[ -L "$APP/ooxmlverify" ] && { echo "driver path is a symlink" >&2; report 0; }
find "$APP/ooxmlverify" -name '*.go' -type f -print0 \
    | xargs -0 -r sed -i "s|__SEEDCALC_MODULE__|$MODPATH|g"
chown_work

# ------------------------------------------------------------- group runs ---
# groups.tsv: name <TAB> kind <TAB> tier <TAB> weight <TAB> expected_cases <TAB> title
#
#   kind = gate    unpriced veto. Must score exactly 1.0 or the step is zero.
#                  Its weight column is ignored and written as 0.
#   kind = scored  contributes weight * score to the weighted pass fraction.
#
# tier (build/regression/new/peripheral) is documentation: it says WHY a group
# is where it is, and nothing reads it but the report.
DETAILS=$WORK/details.jsonl
GATEFAIL=$WORK/gatefail.jsonl
: > "$DETAILS"; : > "$GATEFAIL"
total_w=0; earned_w=0; ngroups=0; ngate=0; nscored=0; GATE=1

json_escape() { printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g' | tr -d '\t\r\n'; }

emit() {  # emit name kind tier weight passed expected executed score note
    printf '{"group":"%s","kind":"%s","tier":"%s","weight":%s,"passed":%s,"expected":%s,"executed":%s,"score":%s,"note":"%s"}\n' \
        "$1" "$2" "$3" "$4" "$5" "$6" "$7" "$8" "$(json_escape "$9")" >> "$DETAILS"
}

# tally <name> <kind> <tier> <weight> <passed> <expected> <executed> <score> <note>
#
# The single place the two halves of the model diverge. A gate group is
# recorded, checked for a perfect pass, and contributes no weight either way.
# A scored group contributes weight to the denominator and weight*score to the
# numerator, and can never veto.
tally() {
    local name=$1 kind=$2 tier=$3 weight=$4 score=$8 note=$9
    if [ "$kind" = gate ]; then
        ngate=$((ngate + 1))
        emit "$name" gate "$tier" 0 "$5" "$6" "$7" "$score" "$note"
        if [ "$(awk -v s="$score" 'BEGIN{print (s >= 0.9999999) ? 1 : 0}')" != 1 ]; then
            GATE=0
            printf '{"group":"%s","tier":"%s","passed":%s,"expected":%s,"executed":%s,"reason":"%s"}\n' \
                "$name" "$tier" "$5" "$6" "$7" \
                "$(json_escape "${note:-gate group did not pass completely}")" >> "$GATEFAIL"
        fi
    else
        nscored=$((nscored + 1))
        total_w=$(awk -v a="$total_w" -v b="$weight" 'BEGIN{printf "%.6f", a+b}')
        earned_w=$(awk -v a="$earned_w" -v w="$weight" -v s="$score" 'BEGIN{printf "%.6f", a + w*s}')
        emit "$name" scored "$tier" "$weight" "$5" "$6" "$7" "$score" "$note"
    fi
}

# The group list is read on fd 3, not stdin, so nothing this loop spawns can
# consume group rows.  `go test` and `go build` inherit the loop's stdin; a
# child that read it would silently swallow the rest of groups.tsv and the
# run would score a truncated ladder as if it were complete.  No current
# driver reads stdin -- this is insulation against a future one.
while IFS=$'\t' read -r gname gkind gtier gweight gexp gtitle <&3; do
    case "$gname" in ''|\#*) continue ;; esac
    ngroups=$((ngroups + 1))
    case "$gkind" in gate|scored) ;; *) gkind=scored ;; esac

    if [ "$gname" = build ]; then
        if [ "$BUILD_OK" = 1 ]; then
            tally build "$gkind" "$gtier" "$gweight" 1 1 1 1.000000 \
                "library builds (-mod=$BUILD_MODE, pin_restored=$PIN_RESTORED)"
        else
            tally build "$gkind" "$gtier" "$gweight" 0 1 1 0.000000 \
                "go build failed: $(tail -1 "$BUILD_LOG" 2>/dev/null | cut -c1-160)"
        fi
        continue
    fi

    if [ "$BUILD_OK" != 1 ]; then
        tally "$gname" "$gkind" "$gtier" "$gweight" 0 "$gexp" 0 0.000000 "candidate library did not build"
        continue
    fi
    if [ ! -d "$APP/ooxmlverify/$gname" ]; then
        tally "$gname" "$gkind" "$gtier" "$gweight" 0 "$gexp" 0 0.000000 "driver group missing from image"
        continue
    fi

    out=$WORK/group-$gname.log
    go_run "$GROUP_TIMEOUT" go test -v -count=1 -vet=off -timeout "${GROUP_TIMEOUT}s" \
        "./ooxmlverify/$gname/" >"$out" 2>&1
    passed=$(grep -c '^--- PASS: Test' "$out" || true)
    failed=$(grep -c '^--- FAIL: Test' "$out" || true)
    skipped=$(grep -c '^--- SKIP: Test' "$out" || true)
    executed=$((passed + failed + skipped))

    note=""
    if [ "$executed" -ne "$gexp" ]; then
        # Executed-count assertion. A compile error, a panic, a timeout, a
        # skipped case or a renamed case all land here, and all of them score
        # the group zero rather than passing vacuously.
        score=0.000000
        note="executed $executed of $gexp expected cases: $(grep -m1 -E '^(#|FAIL|panic:|.*\.go:[0-9]+:)' "$out" 2>/dev/null | cut -c1-160)"
    else
        score=$(awk -v p="$passed" -v t="$gexp" 'BEGIN{ if (t<=0) print "0.000000"; else printf "%.6f", p/t }')
        [ "$failed" -gt 0 ] && note="$(grep -m3 '^--- FAIL: Test' "$out" | tr '\n' ';' | cut -c1-200)"
    fi
    tally "$gname" "$gkind" "$gtier" "$gweight" "$passed" "$gexp" "$executed" "$score" "$note"
    # Keep a trimmed per-group log for diagnosis; never the whole thing.
    { grep -E '^(--- (PASS|FAIL|SKIP)|FAIL|ok |panic:|# )' "$out" | head -300; } \
        > "$LOGS/group-$gname.log" 2>/dev/null || true
done 3< /tests/groups.tsv

# A step with no scored groups would divide by zero. That is an authoring bug,
# not a candidate failure, but it must not read as a pass: score 0 either way.
SCORE=$(awk -v e="$earned_w" -v t="$total_w" 'BEGIN{ if (t<=0) print "0.000000"; else printf "%.6f", e/t }')
REWARD=$(awk -v g="$GATE" -v s="$SCORE" 'BEGIN{printf "%.6f", g*s}')

{
  printf '{\n  "milestone": "%s",\n  "group_count": %s,\n  "gate_group_count": %s,\n  "scored_group_count": %s,\n  "build_mode": "%s",\n  "pin_restored": %s,\n  "module_path": "%s",\n  "weight_total": %s,\n  "weight_earned": %s,\n  "gate": %s,\n  "score": %s,\n  "reward": %s,\n  "gate_failures": [\n' \
      "$STEP" "$ngroups" "$ngate" "$nscored" "$BUILD_MODE" "$PIN_RESTORED" "$MODPATH" \
      "$total_w" "$earned_w" "$GATE" "$SCORE" "$REWARD"
  awk 'NR>1{printf ",\n"} {printf "    %s", $0}' "$GATEFAIL"
  [ -s "$GATEFAIL" ] && printf '\n'
  printf '  ],\n  "groups": [\n'
  awk 'NR>1{printf ",\n"} {printf "    %s", $0}' "$DETAILS"
  printf '\n  ]\n}\n'
} > "$LOGS/reward_details.json"

report "$REWARD"
