#!/usr/bin/env bash
# Verifier entry point. One copy per step; what differs between steps is
# assets/groups.txt, assets/harness/tests/*.rs, assets/harness/Cargo.toml and
# the STEP/RELEASE values at the top.
#
# Runs in this step's verifier container on a fresh copy of /app. Nothing
# written here reaches the next step; only /logs/verifier is read back.
set -u
source /tests/helpers.sh

STEP=07
RELEASE=v1.1.0

# Before anything else: this tree and the image around it must be the same
# milestone. Everything below - the asset manifest included - is only
# meaningful once that holds.
#
# Placement is load bearing, between two lines that must not move:
#   AFTER  `source /tests/helpers.sh`, which defines require_step; and
#   BEFORE `trap on_exit EXIT`, because after the trap require_step's exit
#          would run on_exit -> finish -> report, overwriting reward.json with
#          a generic all-groups scorecard. The reward would still be 0, but
#          "verifier_error":"step_mismatch" would be destroyed and the fault
#          would hide behind a second symptom.
require_step "$STEP"

log_dir=/tmp/icechunk-verifier
harness=/tmp/icechunk-harness
rm -rf -- "$log_dir" "$harness"
mkdir -p "$log_dir" "$log_dir/case-logs"

app=/app
export CARGO_TERM_COLOR=never
# DETERMINISM RULE - and the only place the network is still reachable.
#
# Every cargo invocation below passes --offline, or --locked, or both. Not one
# of them RESOLVES against the live crates.io index:
#
#   --offline  the resolution universe is the registry cache baked into this
#              step's image, whose content is fixed by tests/Dockerfile.
#   --locked   the resolution is the one already written in the lockfile being
#              used; cargo refuses to change it and may only DOWNLOAD the exact
#              versions it names. A published version is immutable on
#              crates.io, so fetching a pinned one is reproducible - resolving
#              is not.
#
# That is the whole of the network's reach: the `--locked` rungs of the two
# build ladders below, the `cargo fetch --locked` that primes the registry
# cache for them, and nothing else.
#
# WHY. A plain, network-enabled `cargo build` re-resolves whatever the
# candidate's lockfile does not already pin, at whatever moment grading runs,
# so the SAME candidate workspace could score differently on different days: a
# dependency can re-resolve to a version whose API has changed, or to one
# declaring a `rust-version` newer than this image's pinned toolchain, and the
# result is indistinguishable from a candidate that cannot compile its own
# code. An online `cargo fetch` that resolves the DRIVERS' own dependency graph
# against the live index has the same problem.
#
# A candidate that ships a lockfile consistent with its manifests - the Oracle
# included - is graded through `--locked`, so the graph it is built with is the
# graph it developed against, on every day. A candidate whose lockfile cargo
# would have to CHANGE has only the image's fixed registry cache to re-resolve
# inside, and may fail its build where a live resolution would have rescued
# it. That is the documented contract: WORKSPACE_HANDOFF.md asks for
# /app/Cargo.lock to stay committed and consistent with the manifests, and for
# an updated lock to be left in place after any manifest change.
export CARGO_NET_OFFLINE=false
export CARGO_TARGET_DIR="$harness/target"
# Where the candidate's `icechunk` binary lands once CARGO_TARGET_DIR is
# redirected. Drivers that exercise the command-line interface read this.
export ICECHUNK_TEST_CLI_BIN="$harness/target/debug/icechunk"

# Which dependency resolution the drivers were built with - `pinned`,
# `candidate`, `fresh`, or `none` when no resolution built them.
resolution=none
resolution_log="$log_dir/resolution.log"
# Which compile-unit shape produced the group results: `aggregate` when every
# group's cases shared the single `conformance` target - the Oracle's path and
# every healthy candidate's path - or `per-group` when that target did not
# compile and each group's cases were then compiled on their own so the failure
# could be attributed to the group it belongs to. Diagnostic only: it changes
# no group name, no case count, no weight and no denominator.
attribution=aggregate
: > "$resolution_log"
note() { printf '%s\n' "$*" >> "$resolution_log"; }

# ---------------------------------------------------------------------------
# Reporting
#
# The step's score is a WEIGHTED pass fraction over independent groups:
#
#     score = (sum of the weights of the groups that passed)
#             / (sum of the weights of every declared group)
#
# groups.txt carries the weight of each group in its third column: this step's
# own new capability carries the bulk of the number, its inherited-regression
# groups a minority share, and peripheral breadth the remainder. The score is
# recorded in reward_details.json; report() in helpers.sh turns it into the
# step's reward, which is 1 only when the score is 1.
# ---------------------------------------------------------------------------
passed_w=0
total_w=0
passed=0
total=0
group_json=''
recorded=','
finished=0

record() {  # record <group> <0=pass|1=fail> <detail>
    local name="$1" status="$2" detail="${3:-}" bit=0 w="${GROUP_WEIGHT[$1]:-1}"
    local tier="${GROUP_TIER[$1]:-new}"
    total=$((total + 1))
    total_w=$((total_w + w))
    if [ "$status" -eq 0 ]; then
        passed=$((passed + 1)); passed_w=$((passed_w + w)); bit=1
    fi
    group_json="${group_json}${group_json:+,}\"$name\":{\"pass\":$bit,\"weight\":$w,\"tier\":\"$tier\"}"
    recorded="${recorded}${name},"
    printf '%s %s w=%s tier=%s %s\n' \
        "$([ "$bit" -eq 1 ] && echo PASS || echo FAIL)" "$name" "$w" "$tier" "$detail" \
        >> "$log_dir/groups.txt"
}

write_details() {  # write_details <score>
    printf '{"step":"%s","release":"%s","score":%s,"resolution":"%s","attribution":"%s","passed_groups":%s,"total_groups":%s,"passed_weight":%s,"total_weight":%s,"groups":{%s}}\n' \
        "$STEP" "$RELEASE" "$1" "$resolution" "$attribution" "$passed" "$total" \
        "$passed_w" "$total_w" "$group_json" > /logs/verifier/reward_details.json
}

finish() {  # finish
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
    for b in build build-cli harness gate assets resolution attrib; do
        [ -s "$log_dir/$b.log" ] && tail -c 20000 "$log_dir/$b.log" > "/logs/verifier/$b.log"
    done
    score=$(awk -v n="$passed_w" -v d="$total_w" \
        'BEGIN { if (d <= 0) printf "0.000000"; else printf "%.6f", n / d }')
    write_details "$score"
    report "$score"
}

# ---------------------------------------------------------------------------
# Every group is declared up front from groups.txt and reported exactly once.
#
# groups.txt lists every SCORED group, its expected case count and its weight:
#
#     <group prefix>  <expected case count>  <weight>  <tier>
#
# Two kinds of group live in it and they are driven by different compile
# targets:
#
#   the conformance groups   one function-name prefix each inside
#                            harness/tests/conformance.rs, run by one filtered
#                            `cargo test` apiece. These are GROUP_NAMES, and
#                            CASE_TOTAL - the number the attribution slice
#                            below checks itself against - counts only these.
#   `gate-endtoend`          the whole of harness/tests/gate.rs: this step's
#                            headline capability at proof-of-life depth plus
#                            one end-to-end path through every milestone this
#                            step inherits.
#
# `tier` is `new`, `inherited` or `peripheral`. It is recorded in
# reward_details.json so the result can be read tier by tier; it does not
# affect the arithmetic, which uses `weight` alone.
#
# ALL_GROUPS is every scored group in report order, and it is what the
# denominator, fail_everything and the crash trap use.
# ---------------------------------------------------------------------------
GATE_GROUP=gate-endtoend
declare -a ALL_GROUPS=()
declare -a GROUP_NAMES=()
declare -A GROUP_WANT=()
declare -A GROUP_WEIGHT=()
declare -A GROUP_TIER=()
CASE_TOTAL=0
gate_declared=0
cli_needed=0
while read -r gname gcount gweight gtier; do
    case "$gname" in ''|\#*) continue ;; esac
    ALL_GROUPS+=("$gname")
    GROUP_WANT["$gname"]=$gcount
    GROUP_WEIGHT["$gname"]=${gweight:-1}
    GROUP_TIER["$gname"]=${gtier:-new}
    case "$gname" in *cli*) cli_needed=1 ;; esac
    if [ "$gname" = "$GATE_GROUP" ]; then
        gate_declared=1
        continue
    fi
    GROUP_NAMES+=("$gname")
    CASE_TOTAL=$((CASE_TOTAL + gcount))
done < /tests/assets/groups.txt

fail_everything() {  # fail_everything <reason>
    : > "$log_dir/groups.txt"
    passed=0; total=0; passed_w=0; total_w=0; group_json=''; recorded=','
    local g
    for g in "${ALL_GROUPS[@]}"; do record "$g" 1 "$1"; done
    finish
}

# A crash, kill or unexpected exit still produces a complete scorecard.
on_exit() {
    [ "$finished" -eq 0 ] || return 0
    local g
    for g in "${ALL_GROUPS[@]}"; do
        case "$recorded" in *",$g,"*) continue ;; esac
        record "$g" 1 "verifier-aborted-before-this-group"
    done
    finish
}
# A hard wall-clock kill from the harness arrives as SIGTERM, which bash does
# not route through the EXIT trap on its own. Without this a timed-out verifier
# writes no reward.json at all, which reads as an infrastructure error rather
# than as the partial result it is. With it, every group that had not yet been
# reported is recorded as a failure and a complete scorecard is still written.
on_signal() { on_exit; exit 143; }
trap on_exit EXIT
trap on_signal TERM INT

: > "$log_dir/groups.txt"

# ---------------------------------------------------------------------------
# Asset integrity. A corrupt driver corpus must fail every group rather than
# hand out free points, so it is a prerequisite, not a group of its own.
# ---------------------------------------------------------------------------
( cd /tests/assets && sha256sum -c SHA256SUMS ) > "$log_dir/assets.log" 2>&1
assets_ok=$?
driver_total=$(find /tests/assets/harness -type f -name '*.rs' | wc -l)
expected_driver_total=$(cat /tests/assets/DRIVER_COUNT)
gate_want=$(cat /tests/assets/GATE_COUNT)
printf 'assets sha check=%s drivers=%s expected=%s gate_declared=%s gate_want=%s groups_gate=%s cli_needed=%s\n' \
    "$assets_ok" "$driver_total" "$expected_driver_total" \
    "$gate_declared" "$gate_want" "${GROUP_WANT[$GATE_GROUP]:--}" "$cli_needed" >> "$log_dir/assets.log"
if [ "$assets_ok" -ne 0 ] || [ "$driver_total" != "$expected_driver_total" ]; then
    fail_everything "verifier driver corpus failed its integrity check"
fi
if [ "$gate_declared" -ne 1 ] || [ "${GROUP_WANT[$GATE_GROUP]}" != "$gate_want" ]; then
    fail_everything "groups.txt does not declare $GATE_GROUP with GATE_COUNT cases"
fi

# ---------------------------------------------------------------------------
# Build the candidate workspace. /app is untrusted candidate data: never follow
# a symlink out of it, and write only inside the throw-away harness directory.
#
# `cargo build` at /app builds the workspace's default members, which is the
# `icechunk` library crate. When this step scores the command-line interface a
# second build adds the `cli` feature, because upstream gates the `icechunk`
# binary behind it; that build's failure costs only the CLI groups.
#
# THE ATTEMPT LADDERS, and why they are shaped this way
# ----------------------------------------------------
# The candidate's OWN Cargo.lock is the only lockfile either build ever uses.
# Nothing is staged over it, so there is no lock to preserve and restore: the
# resolution being graded is the one the candidate committed, which is what the
# handoff contract asks for. Each rung is a deterministic function of (this
# image, the candidate's tree):
#
#   candidate-offline   the candidate's own lock, `--locked --offline`. Fully
#                       hermetic: nothing is resolved and nothing is
#                       downloaded. A candidate whose lock names only versions
#                       this image already caches - the Oracle, whose lock IS
#                       the committed lock the image's cache was fetched from,
#                       and every candidate that added no dependency - stops
#                       here and never touches the network.
#   candidate-locked    the same lock, `--locked`, network allowed, preceded by
#                       the `cargo fetch --locked` priming below. cargo may
#                       DOWNLOAD the exact versions the lock names and may not
#                       change one of them, so the graph is the candidate's own
#                       whatever crates.io has published since. This is the
#                       rung a candidate that legitimately ADDED a dependency
#                       and re-locked takes.
#   plain-offline       `--offline`, no lock constraint. The last resort for a
#                       lock cargo would have to change: cargo may re-resolve,
#                       but only inside a registry cache whose content is fixed
#                       by this image plus the priming fetch above, so the
#                       outcome still does not depend on the day.
#
# There is no network-RESOLVING rung. A candidate whose lockfile cargo would
# have to change, and whose replacement versions this image does not cache,
# fails its build rather than being re-resolved off the live index: the verdict
# is the same on every day, at the cost of a candidate that did not leave the
# consistent lock the handoff contract asks for.
#
# The `--features cli` build runs the same three rungs over its own manifest.
# It cannot simply inherit the graded build's mode: `cargo build` downloads
# only what it compiles, and `--features cli` activates optional dependencies
# the library build never touched, so a hermetic graded build can be followed
# by a CLI build that still needs the priming fetch.
#
# BUDGET. Each ladder shares ONE deadline, and those two deadlines are exactly
# the two numbers task.toml's verifier ceiling already budgets - 1800 s for the
# candidate build and 1200 s for the `--features cli` build - so the ladders
# cannot widen the verifier's worst case and the arithmetic in task.toml is
# unchanged however many rungs run.
# ---------------------------------------------------------------------------
build_ok=1
cli_build_ok=1
build_how=none
cli_how=none
primed=0

BUILD_BUDGET=1800
CLI_BUDGET=1200
PRIME_MAX=600

# prime_cache <deadline> <logfile>
#
# Downloads the candidate's ENTIRE locked graph - every target, not just the
# host's, and every optional dependency, not just the activated ones - before
# any networked build. Three consumers need it and none can do it for itself:
# the `--locked` rungs, the `--features cli` build, and the drivers' own
# `cargo fetch --offline` completion further down, which resolves over ALL
# targets and would otherwise be short of a crate the candidate's lock already
# names - the drivers would then be graded `none` against a workspace that
# built perfectly well. Here that is `wit-bindgen` and `wasip2`, which a
# host-target `cargo build` never downloads.
#
# `--locked` makes this a DOWNLOAD of the versions the lockfile names and never
# a resolution: cargo refuses to change the lock, so what lands in the cache is
# a function of the candidate's lockfile and not of the day. It runs at most
# once, and its status is deliberately ignored - a fetch that cannot complete
# must never cost a candidate a build it would otherwise have had.
prime_cache() {
    local limit
    [ "$primed" -eq 0 ] || return 0
    primed=1
    limit=$(($1 - $(date +%s)))
    [ "$limit" -gt "$PRIME_MAX" ] && limit=$PRIME_MAX
    if [ "$limit" -le 5 ]; then
        printf '\n--- priming the registry cache: skipped, no budget left ---\n' >> "$2"
        return 0
    fi
    printf '\n--- priming the registry cache: cargo fetch --locked ---\n' >> "$2"
    ( cd "$app" && timeout "$limit" cargo fetch --locked ) >> "$2" 2>&1
    printf 'cargo fetch --locked: status=%s\n' "$?" >> "$2"
}

# one_build <logfile> <deadline> <budget> <how> <cargo args...>
one_build() {
    local logf="$1" deadline="$2" budget="$3" how="$4" limit rc
    shift 4
    limit=$((deadline - $(date +%s)))
    [ "$limit" -gt "$budget" ] && limit=$budget
    if [ "$limit" -le 5 ]; then
        printf '\n--- %s: skipped, the %ss build budget was exhausted ---\n' \
            "$how" "$budget" >> "$logf"
        return 1
    fi
    printf '\n--- %s: cargo build %s ---\n' "$how" "$*" >> "$logf"
    ( cd "$app" && timeout "$limit" cargo build "$@" ) >> "$logf" 2>&1
    rc=$?
    printf '%s: status=%s\n' "$how" "$rc" >> "$logf"
    return $rc
}

if [ -L "$app" ] || [ ! -f "$app/Cargo.toml" ]; then
    echo "no /app/Cargo.toml" > "$log_dir/build.log"
else
    : > "$log_dir/build.log"
    build_deadline=$(($(date +%s) + BUILD_BUDGET))
    build_how=candidate-offline
    one_build "$log_dir/build.log" "$build_deadline" "$BUILD_BUDGET" candidate-offline \
        --manifest-path "$app/Cargo.toml" --locked --offline
    build_ok=$?
    if [ "$build_ok" -ne 0 ]; then
        prime_cache "$build_deadline" "$log_dir/build.log"
        build_how=candidate-locked
        one_build "$log_dir/build.log" "$build_deadline" "$BUILD_BUDGET" candidate-locked \
            --manifest-path "$app/Cargo.toml" --locked
        build_ok=$?
    fi
    if [ "$build_ok" -ne 0 ]; then
        build_how=plain-offline
        one_build "$log_dir/build.log" "$build_deadline" "$BUILD_BUDGET" plain-offline \
            --manifest-path "$app/Cargo.toml" --offline
        build_ok=$?
    fi
fi
printf 'cargo build status=%s how=%s\n' "$build_ok" "$build_how" >> "$log_dir/build.log"

if [ "$cli_needed" -eq 1 ] && [ "$build_ok" -eq 0 ]; then
    : > "$log_dir/build-cli.log"
    cli_deadline=$(($(date +%s) + CLI_BUDGET))
    cli_how=cli-offline
    one_build "$log_dir/build-cli.log" "$cli_deadline" "$CLI_BUDGET" cli-offline \
        --manifest-path "$app/icechunk/Cargo.toml" --features cli --bin icechunk \
        --locked --offline
    cli_build_ok=$?
    if [ "$cli_build_ok" -ne 0 ]; then
        prime_cache "$cli_deadline" "$log_dir/build-cli.log"
        cli_how=cli-locked
        one_build "$log_dir/build-cli.log" "$cli_deadline" "$CLI_BUDGET" cli-locked \
            --manifest-path "$app/icechunk/Cargo.toml" --features cli --bin icechunk \
            --locked
        cli_build_ok=$?
    fi
    if [ "$cli_build_ok" -ne 0 ]; then
        cli_how=cli-plain-offline
        one_build "$log_dir/build-cli.log" "$cli_deadline" "$CLI_BUDGET" cli-plain-offline \
            --manifest-path "$app/icechunk/Cargo.toml" --features cli --bin icechunk \
            --offline
        cli_build_ok=$?
    fi
    printf 'cargo build --features cli --bin icechunk status=%s how=%s bin=%s\n' \
        "$cli_build_ok" "$cli_how" "$ICECHUNK_TEST_CLI_BIN" >> "$log_dir/build-cli.log"
fi

# ---------------------------------------------------------------------------
# Stage the verifier-owned drivers outside /app and link them to the candidate
# by path. The candidate's manifest is never modified.
#
# The two driver targets are compiled SEPARATELY and reported separately, so
# `gate-endtoend`'s outcome is never coupled to whether the conformance driver
# compiles.
#
# Choosing the drivers' dependency resolution
# -------------------------------------------
# The drivers depend on the candidate crate BY PATH, so one resolution has to
# satisfy the verifier's driver manifest AND the candidate's own manifest at
# once. Feeding one lockfile to the other's manifest makes cargo keep every
# version that still fits and re-resolve the rest, producing a graph neither
# side ever built. So: exactly one lockfile is ever present in the harness,
# every attempt starts from a harness with NO lockfile, and every build runs
# `--locked` - cargo refuses a resolution it would have to change instead of
# splicing one. The attempts, in order:
#
#   pinned     the task-owned lock, built `--offline --locked`. Nothing is
#              resolved and nothing is downloaded, so nothing in /app can break
#              or influence it. Every candidate that left the manifests alone -
#              the Oracle included - takes this path and only this path.
#   candidate  the candidate's own lock as the sole seed, completed by ONE
#              resolver run that can only add packages no lockfile supplied at
#              all - the driver's own dependencies - then frozen with
#              `--locked`.
#   fresh      no seed at all: one complete re-resolution of both manifests
#              together, then frozen with `--locked`.
#
# Both completions run `cargo fetch --offline` and neither is retried online.
# The universe they resolve inside is the image's registry cache plus whatever
# the build ladder's `cargo fetch --locked` priming downloaded - two fixed
# inputs - so no resolution here depends on the live index or on the day.
#
# The resolution is chosen ONCE, before a single line of driver code is
# compiled, by a probe that builds the whole dependency graph and none of the
# drivers.
gate_build_ok=1
conf_build_ok=1

RESOLVE_BUDGET=1500
PROBE_MAX=1200
DRIVER_BUILD_MAX=1200
resolve_deadline=0

# Sized so that every group can still be sliced and compiled on its own. Each
# slice reuses the dependency graph the probe already built, so it costs only
# the driver's own compile unit - but there is one slice per conformance group,
# and a budget sized for the typical case could run out before the last groups
# were reached and record them `drivers-did-not-compile` rather than running
# them. The budget is therefore set well above the worst case.
ATTRIB_BUDGET=2400
ATTRIB_BUILD_MAX=300
attrib_deadline=0

try_resolution() {  # try_resolution <name> <pinned|candidate|none> <complete: yes|no>
    local name="$1" seed="$2" complete="$3" limit rc
    limit=$((resolve_deadline - $(date +%s)))
    [ "$limit" -gt "$PROBE_MAX" ] && limit=$PROBE_MAX
    if [ "$limit" -le 5 ]; then
        note "$name: skipped, the ${RESOLVE_BUDGET}s resolution budget was exhausted"
        return 1
    fi
    rm -f -- "$harness/Cargo.lock"
    case "$seed" in
    pinned)
        cp /tests/assets/harness/Cargo.lock.fallback "$harness/Cargo.lock"
        ;;
    candidate)
        if [ ! -f "$app/Cargo.lock" ] || [ -L "$app/Cargo.lock" ]; then
            note "$name: skipped, the candidate has no regular Cargo.lock"
            return 1
        fi
        cp -- "$app/Cargo.lock" "$harness/Cargo.lock"
        ;;
    none) ;;
    esac
    if [ "$complete" = yes ]; then
        # Complete the seed from the image's pre-populated registry cache, plus
        # whatever the `cargo fetch --locked` priming above downloaded. Both are
        # fixed inputs - the image, and the candidate's own lockfile - so the
        # graph the drivers are graded with is a function of the candidate's
        # tree and this image, and of nothing else.
        #
        # There is no online retry here: it would resolve the drivers' own
        # dependency graph against the live index and make the result depend
        # on the day. The priming fetch above covers that case instead: a
        # candidate that ADDED a dependency the image never saw already has
        # that dependency's exact locked version in the cache before this
        # completion runs.
        ( cd "$harness" && timeout "$limit" cargo fetch --offline ) \
            > "$log_dir/resolve-$name.log" 2>&1
        rc=$?
        if [ "$rc" -ne 0 ]; then
            note "$name: rejected, the resolution itself failed (cargo fetch status=$rc)"
            tail -c 4000 "$log_dir/resolve-$name.log" >> "$resolution_log"
            return 1
        fi
        limit=$((resolve_deadline - $(date +%s)))
        [ "$limit" -gt "$PROBE_MAX" ] && limit=$PROBE_MAX
        if [ "$limit" -le 5 ]; then
            note "$name: skipped, no budget left to verify the resolution"
            return 1
        fi
    fi
    ( cd "$harness" && timeout "$limit" cargo build --locked --offline ) \
        > "$log_dir/probe-$name.log" 2>&1
    rc=$?
    if [ "$rc" -ne 0 ]; then
        note "$name: rejected, the dependency graph did not build (status=$rc)"
        tail -c 4000 "$log_dir/probe-$name.log" >> "$resolution_log"
        return 1
    fi
    note "$name: accepted"
    return 0
}

: > "$log_dir/harness.log"
if [ "$build_ok" -eq 0 ]; then
    mkdir -p "$harness/tests" "$harness/src"
    cp /tests/assets/harness/Cargo.toml "$harness/Cargo.toml"
    cp /tests/assets/harness/tests/*.rs "$harness/tests/"
    # The harness lib target deliberately holds no code. It exists so that
    # `cargo build` has something to build, and therefore compiles the whole
    # dependency graph, without compiling any driver code - see the probe above.
    printf '// Verifier-owned resolution probe: no code of its own, so that\n' \
        > "$harness/src/lib.rs"
    printf '// `cargo build` compiles the dependency graph and none of the drivers.\n' \
        >> "$harness/src/lib.rs"

    resolve_deadline=$(($(date +%s) + RESOLVE_BUDGET))
    note "driver dependency resolution, in preference order, budget ${RESOLVE_BUDGET}s"
    if try_resolution pinned pinned no; then
        resolution=pinned
    elif try_resolution candidate candidate yes; then
        resolution=candidate
    elif try_resolution fresh none yes; then
        resolution=fresh
    else
        resolution=none
    fi
    note "chosen: $resolution"

    if [ "$resolution" = none ]; then
        note "no internally consistent resolution built the drivers' dependency graph"
        echo "no dependency resolution built the drivers' graph; see resolution.log" \
            >> "$log_dir/harness.log"
    else
        build_target() {  # build_target <gate|conformance>
            ( cd "$harness" && timeout "$DRIVER_BUILD_MAX" \
                cargo test --no-run --locked --offline --test "$1" ) \
                >> "$log_dir/harness.log" 2>&1
            local rc=$?
            printf 'cargo test --no-run --locked --offline --test %s status=%s\n' "$1" "$rc" \
                >> "$log_dir/harness.log"
            return $rc
        }
        build_target gate; gate_build_ok=$?
        build_target conformance; conf_build_ok=$?

        # One compile error anywhere in the driver zeroes the whole
        # `conformance` target, and with it every scored group, because they
        # share ONE compile unit. So when, and only when, the aggregate target
        # fails to compile, each group's cases are compiled on their own below
        # and the failure is attributed to the group it belongs to. This costs
        # nothing on the path every healthy candidate takes.
        if [ "$conf_build_ok" -ne 0 ]; then
            attribution=per-group
            attrib_deadline=$(($(date +%s) + ATTRIB_BUDGET))
            note "the aggregate conformance target did not compile against the candidate;" \
                "attributing the failure per group, budget ${ATTRIB_BUDGET}s"
        fi
    fi
else
    echo "candidate did not build; drivers not compiled" >> "$log_dir/harness.log"
fi
printf 'driver dependency resolution: %s\n' "$resolution" >> "$log_dir/harness.log"

# ---------------------------------------------------------------------------
# Failure attribution: one compile unit per group, cut from the shipped driver.
#
# The cut is subtractive and it is made from the integrity-checked driver every
# time, never from a previous cut. For a target group it inserts
# `#[cfg(any())]` - an always-false configuration predicate, so the item is
# removed before it is type-checked - ahead of the attribute block of every
# case function belonging to a DIFFERENT group. Nothing else in the file is
# touched. A case function is recognised only when the prefix before its `__`
# is a group named in groups.txt, and the number of removed cases is checked
# against groups.txt afterwards; if either check does not hold the slice is
# refused and the group is reported as the aggregate driver failure.
SLICE_AWK='
BEGIN { n = split(groups, gs, " "); for (i = 1; i <= n; i++) known[gs[i]] = 1 }
{ line[NR] = $0 }
END {
    for (i = 1; i <= NR; i++) {
        name = line[i]
        if (name !~ /^(async[ \t]+)?fn[ \t]+[A-Za-z_][A-Za-z0-9_]*__/) continue
        sub(/^(async[ \t]+)?fn[ \t]+/, "", name)
        sub(/__.*$/, "", name)
        if (!(name in known) || name == keep) continue
        j = i - 1
        while (j >= 1 && line[j] ~ /^[ \t]*#\[/) j--
        cut[j + 1] = 1
    }
    for (i = 1; i <= NR; i++) {
        if (i in cut) print "#[cfg(any())] // attribution slice: case of another group"
        print line[i]
    }
}'
SLICE_MARK='#[cfg(any())] // attribution slice:'

# attrib_compile <group> <case-log>
#   0  this group's cases compiled on their own; run them
#   1  this group's own cases do not compile against the candidate
#   2  the failure could not be attributed; report it as the aggregate failure
attrib_compile() {
    local name="$1" logf="$2" limit rc removed expected
    local src=/tests/assets/harness/tests/conformance.rs
    limit=$((attrib_deadline - $(date +%s)))
    [ "$limit" -gt "$ATTRIB_BUILD_MAX" ] && limit=$ATTRIB_BUILD_MAX
    if [ "$limit" -le 5 ]; then
        printf '%s: skipped, the %ss attribution budget was exhausted\n' \
            "$name" "$ATTRIB_BUDGET" >> "$log_dir/attrib.log"
        return 2
    fi
    if ! awk -v keep="$name" -v groups="${GROUP_NAMES[*]}" "$SLICE_AWK" "$src" \
            > "$harness/tests/conformance.rs.slice" 2>> "$log_dir/attrib.log"; then
        printf '%s: refused, the slice could not be cut\n' "$name" >> "$log_dir/attrib.log"
        rm -f -- "$harness/tests/conformance.rs.slice"
        return 2
    fi
    removed=$(grep -cF "$SLICE_MARK" "$harness/tests/conformance.rs.slice")
    expected=$((CASE_TOTAL - GROUP_WANT[$name]))
    if [ "$removed" -ne "$expected" ]; then
        printf '%s: refused, the slice removed %s cases and groups.txt expects %s\n' \
            "$name" "$removed" "$expected" >> "$log_dir/attrib.log"
        rm -f -- "$harness/tests/conformance.rs.slice"
        return 2
    fi
    mv -- "$harness/tests/conformance.rs.slice" "$harness/tests/conformance.rs"
    ( cd "$harness" && timeout "$limit" \
        cargo test --no-run --locked --offline --test conformance ) >> "$logf" 2>&1
    rc=$?
    printf '%s: cases=%s removed=%s cargo test --no-run status=%s\n' \
        "$name" "${GROUP_WANT[$name]}" "$removed" "$rc" >> "$log_dir/attrib.log"
    [ "$rc" -eq 0 ] || return 1
    return 0
}

# ---------------------------------------------------------------------------
# Group execution. One `cargo test` per group, filtered by the group's function
# name prefix, with an executed-count assertion on every invocation: a filter
# that matched nothing must never be reported as a pass.
# ---------------------------------------------------------------------------
run_group() {  # run_group <name>
    local name="$1" want="${GROUP_WANT[$1]}"
    local logf="$log_dir/case-logs/$name.log"
    : > "$logf"

    if [ "$build_ok" -ne 0 ]; then
        echo "candidate did not build; see build.log" >> "$logf"
        record "$name" 1 "build-failed"
        return
    fi
    case "$name" in
    *cli*)
        if [ "$cli_build_ok" -ne 0 ]; then
            echo "the candidate's icechunk binary did not build with --features cli; see build-cli.log" >> "$logf"
            record "$name" 1 "cli-build-failed"
            return
        fi
        ;;
    esac
    if [ "$conf_build_ok" -ne 0 ]; then
        local arc=2
        if [ "$attribution" = per-group ]; then
            attrib_compile "$name" "$logf"
            arc=$?
        fi
        case "$arc" in
        0) ;;
        1)
            echo "this group's own cases did not compile against the candidate; the compiler output is above" >> "$logf"
            record "$name" 1 "group-cases-did-not-compile resolution=$resolution"
            return
            ;;
        *)
            echo "the scored drivers did not compile against the candidate; see harness.log, attrib.log and resolution.log" >> "$logf"
            record "$name" 1 "drivers-did-not-compile resolution=$resolution"
            return
            ;;
        esac
    fi

    ( cd "$harness" && timeout 900 \
        cargo test --locked --offline --test conformance -- --test-threads=1 "${name}__" ) \
        >> "$logf" 2>&1
    local rc=$?
    local summary p f
    summary=$(grep -E '^test result:' "$logf" | tail -1)
    p=$(printf '%s' "$summary" | sed -n 's/.* \([0-9][0-9]*\) passed;.*/\1/p')
    f=$(printf '%s' "$summary" | sed -n 's/.* \([0-9][0-9]*\) failed;.*/\1/p')
    p=${p:-0}; f=${f:-0}
    local executed=$((p + f))
    printf 'group %s: rc=%s executed=%s want=%s failed=%s\n' \
        "$name" "$rc" "$executed" "$want" "$f" >> "$logf"
    if [ "$executed" -ne "$want" ] || [ "$f" -ne 0 ] || [ -z "$summary" ]; then
        record "$name" 1 "executed=$executed/$want failed=$f"
    else
        record "$name" 0 "executed=$executed"
    fi
}

for g in "${GROUP_NAMES[@]}"; do run_group "$g"; done

# ---------------------------------------------------------------------------
# `gate-endtoend`: this step's headline capability at proof-of-life depth, plus
# one end-to-end path through every milestone this step inherits. It is one
# ordinary scored group among the others - it multiplies nothing and stops
# nothing - reported from its own compile target so that a conformance driver
# that does not typecheck cannot take it down with it, and vice versa.
# ---------------------------------------------------------------------------
gate_log="$log_dir/gate.log"
: > "$gate_log"
gate_case_log="$log_dir/case-logs/$GATE_GROUP.log"
# The gate drives the `icechunk` binary wherever this step scores the CLI, so a
# failed `--features cli` build has to be attributed here too. Without this the
# gate's cases panic on a missing binary and the group is reported as an
# assertion failure, which is a scoring-attribution defect: a build problem
# would read as a behaviour problem.
gate_cli_blocked=0
if [ "$cli_needed" -eq 1 ] && [ "$cli_build_ok" -ne 0 ]; then
    gate_cli_blocked=1
fi
if [ "$build_ok" -ne 0 ] || [ "$gate_build_ok" -ne 0 ] || [ "$gate_cli_blocked" -ne 0 ]; then
    printf 'gate: candidate did not build, the gate driver did not compile against it, or the icechunk binary did not build with --features cli (resolution=%s)\n' \
        "$resolution" >> "$gate_log"
    cp "$gate_log" "$gate_case_log" 2>/dev/null || true
    if [ "$build_ok" -ne 0 ]; then
        record "$GATE_GROUP" 1 "build-failed"
    elif [ "$gate_cli_blocked" -ne 0 ]; then
        record "$GATE_GROUP" 1 "cli-build-failed"
    else
        record "$GATE_GROUP" 1 "gate-driver-did-not-compile resolution=$resolution"
    fi
else
    ( cd "$harness" && timeout 900 \
        cargo test --locked --offline --test gate -- --test-threads=1 ) >> "$gate_log" 2>&1
    gate_summary=$(grep -E '^test result:' "$gate_log" | tail -1)
    gp=$(printf '%s' "$gate_summary" | sed -n 's/.* \([0-9][0-9]*\) passed;.*/\1/p')
    gf=$(printf '%s' "$gate_summary" | sed -n 's/.* \([0-9][0-9]*\) failed;.*/\1/p')
    gp=${gp:-0}; gf=${gf:-0}
    gate_exec=$((gp + gf))
    printf 'gate: executed=%s want=%s failed=%s\n' "$gate_exec" "$gate_want" "$gf" >> "$gate_log"
    cp "$gate_log" "$gate_case_log" 2>/dev/null || true
    if [ "$gate_exec" -ne "$gate_want" ] || [ "$gf" -ne 0 ] || [ -z "$gate_summary" ]; then
        record "$GATE_GROUP" 1 "executed=$gate_exec/$gate_want failed=$gf"
    else
        record "$GATE_GROUP" 0 "executed=$gate_exec"
    fi
fi

finish
