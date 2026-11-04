#!/usr/bin/env bash

# --- verifier/step binding check -------------------------------------------
# /etc/harbor-step-id names the image; the literal below names this tests/ tree.
# They disagree only when the build context came from another step's cache
# entry, in which case every check below would grade the wrong milestone.
# Reported, not crashed: a non-zero exit makes Harbor skip the remaining steps
# and buries the fault under a second symptom.
__hb_want=03
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
# Verifier entry point. One copy per step; what differs between steps is
# assets/groups.txt, assets/harness/tests/*.rs and the STEP/RELEASE values at
# the top.
#
# Runs in this step's verifier container on a fresh copy of /app. Nothing
# written here reaches the next step; only /logs/verifier is read back.
set -u
source /tests/helpers.sh

STEP=03
RELEASE=0.4.0

log_dir=/tmp/iceberg-verifier
harness=/tmp/iceberg-harness
rm -rf -- "$log_dir" "$harness"
mkdir -p "$log_dir" "$log_dir/case-logs"

app=/app
export CARGO_TERM_COLOR=never
# The network is reachable only where this script asks for it: the candidate's
# own build, and the two fallback resolutions below. Every other cargo
# invocation passes --offline explicitly, so it can neither resolve nor
# download anything.
export CARGO_NET_OFFLINE=false
export CARGO_TARGET_DIR="$harness/target"

# Which dependency resolution the drivers were built with - `pinned`,
# `candidate`, `fresh`, or `none` when no resolution built them. Declared here
# because every report carries it, including the ones written before the
# drivers are staged.
resolution=none
resolution_log="$log_dir/resolution.log"
# Which compile-unit shape produced the group results: `aggregate` when every
# group's cases shared the single `conformance` target - the Oracle's path and
# every healthy candidate's path - or `per-group` when that target did not
# compile and each group's cases were then compiled on their own so the failure
# could be attributed to the group it belongs to. Diagnostic only, exactly like
# `resolution`: it changes no group name, no case count and no denominator.
attribution=aggregate
: > "$resolution_log"
note() { printf '%s\n' "$*" >> "$resolution_log"; }

# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------
passed=0
total=0
group_json=''
recorded=','
finished=0

record() {  # record <group> <0=pass|1=fail> <detail>
    local name="$1" status="$2" detail="${3:-}" bit=0
    total=$((total + 1))
    if [ "$status" -eq 0 ]; then
        passed=$((passed + 1)); bit=1
    fi
    group_json="${group_json}${group_json:+,}\"$name\":$bit"
    recorded="${recorded}${name},"
    printf '%s %s %s\n' "$([ "$bit" -eq 1 ] && echo PASS || echo FAIL)" "$name" "$detail" \
        >> "$log_dir/groups.txt"
}

write_details() {  # write_details <score>
    # `resolution` is diagnostic only - it names which dependency resolution
    # the drivers were built with, so a build failure is attributable from the
    # reward file alone. It does not affect the score, the group names or the
    # denominator.
    printf '{"step":"%s","release":"%s","score":%s,"resolution":"%s","attribution":"%s","passed_groups":%s,"total_groups":%s,"groups":{%s}}\n' \
        "$STEP" "$RELEASE" "$1" "$resolution" "$attribution" "$passed" "$total" \
        "$group_json" > /logs/verifier/reward_details.json
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
    for b in build harness gate assets resolution attrib; do
        [ -s "$log_dir/$b.log" ] && tail -c 20000 "$log_dir/$b.log" > "/logs/verifier/$b.log"
    done
    # This step's score is `passed / total` over the groups declared in
    # groups.txt, `gate-endtoend` included. Nothing here credits a predecessor
    # milestone and nothing aborts the run. `report` (helpers.sh) turns the
    # score into the step reward; the fraction itself is kept in
    # reward_details.json.
    score=$(awk -v n="$passed" -v d="$total" \
        'BEGIN { if (d <= 0) printf "0.000000"; else printf "%.6f", n / d }')
    write_details "$score"
    report "$score"
}

# ---------------------------------------------------------------------------
# Every group is declared up front from groups.txt and reported exactly once.
#
# groups.txt lists every SCORED group and its expected case count. Two kinds of
# group live in it and they are driven by different compile targets:
#
#   the conformance groups   one function-name prefix each inside
#                            harness/tests/conformance.rs, run by one filtered
#                            `cargo test` apiece. These are GROUP_NAMES, and
#                            CASE_TOTAL - the number the attribution slice
#                            below checks itself against - counts only these.
#   `gate-endtoend`          the whole of harness/tests/gate.rs: this step's
#                            headline capability at proof-of-life depth plus
#                            one end-to-end path through every milestone this
#                            step inherits. It is one ordinary scored group.
#
# ALL_GROUPS is every scored group in report order, and it is what the
# denominator, fail_everything and the crash trap use.
# ---------------------------------------------------------------------------
GATE_GROUP=gate-endtoend
declare -a ALL_GROUPS=()
declare -a GROUP_NAMES=()
declare -A GROUP_WANT=()
CASE_TOTAL=0
gate_declared=0
while read -r gname gcount; do
    case "$gname" in ''|\#*) continue ;; esac
    ALL_GROUPS+=("$gname")
    GROUP_WANT["$gname"]=$gcount
    if [ "$gname" = "$GATE_GROUP" ]; then
        gate_declared=1
        continue
    fi
    GROUP_NAMES+=("$gname")
    CASE_TOTAL=$((CASE_TOTAL + gcount))
done < /tests/assets/groups.txt

fail_everything() {  # fail_everything <reason>
    : > "$log_dir/groups.txt"
    passed=0; total=0; group_json=''; recorded=','
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
trap on_exit EXIT

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
printf 'assets sha check=%s drivers=%s expected=%s gate_declared=%s gate_want=%s groups_gate=%s\n' \
    "$assets_ok" "$driver_total" "$expected_driver_total" \
    "$gate_declared" "$gate_want" "${GROUP_WANT[$GATE_GROUP]:--}" >> "$log_dir/assets.log"
if [ "$assets_ok" -ne 0 ] || [ "$driver_total" != "$expected_driver_total" ]; then
    fail_everything "verifier driver corpus failed its integrity check"
fi
# groups.txt and GATE_COUNT are two integrity-checked statements of the same
# number - how many cases gate.rs holds. They must agree, or the end-to-end
# group's executed-count assertion would be checking the wrong denominator.
if [ "$gate_declared" -ne 1 ] || [ "${GROUP_WANT[$GATE_GROUP]}" != "$gate_want" ]; then
    fail_everything "groups.txt does not declare $GATE_GROUP with GATE_COUNT cases"
fi

# ---------------------------------------------------------------------------
# Build the candidate crate. /app is untrusted candidate data: never follow a
# symlink out of it, and write only inside the throw-away harness directory.
# ---------------------------------------------------------------------------
build_ok=1
if [ -L "$app" ] || [ ! -f "$app/Cargo.toml" ]; then
    echo "no /app/Cargo.toml" > "$log_dir/build.log"
else
    ( cd "$app" && timeout 1500 cargo build --manifest-path "$app/Cargo.toml" ) \
        > "$log_dir/build.log" 2>&1
    build_ok=$?
fi
printf 'cargo build status=%s\n' "$build_ok" >> "$log_dir/build.log"

# ---------------------------------------------------------------------------
# Stage the verifier-owned drivers outside /app and link them to the candidate
# by path. The candidate's manifest is never modified.
# ---------------------------------------------------------------------------
# The two driver targets are compiled SEPARATELY and reported separately, so
# `gate-endtoend`'s outcome is never coupled to whether the conformance driver
# compiles: a candidate that satisfies this step's headline capability still
# passes `gate-endtoend` when a conformance group's cases fail to typecheck
# against it, and only a gate.rs compile failure costs that one group.
#
# Choosing the drivers' dependency resolution
# -------------------------------------------
# The drivers depend on the candidate crate BY PATH, so one resolution has to
# satisfy the verifier's driver manifest AND the candidate's own manifest at
# once. Two lockfiles could supply it, and they are not interchangeable:
#
#   /tests/assets/harness/Cargo.lock.fallback
#       the task-owned resolution of the driver manifest against this release's
#       pinned crate manifest. tests/Dockerfile has already fetched every crate
#       in it with `cargo fetch --locked`, so it resolves and builds with no
#       network at all and the candidate cannot influence it.
#   /app/Cargo.lock
#       the candidate's own resolution - the one its own `cargo build` above
#       just used - but it does not contain the driver crate's dependencies.
#
# Feeding one of them to the other's manifest does not work: cargo keeps every
# version that still fits and re-resolves the rest, producing a graph that
# neither side ever built. For example, with the task-owned lock in a harness
# whose path dependency is a candidate pinning `arrow-* = "52"`, cargo
# downgrades the arrow crates to 52.2.0 to satisfy the manifest but keeps the
# task lock's chrono 0.4.41 because `chrono = "0.4"` still accepts it, and
# arrow-arith 52.2.0 does not compile against a chrono that has
# `Datelike::quarter` (E0034, ambiguous `quarter`). Neither lockfile names that
# pair; only the merge of the two does.
#
# So: exactly one lockfile is ever present in the harness, every attempt starts
# from a harness with NO lockfile, and every build runs `--locked` - cargo
# refuses a resolution it would have to change instead of splicing one. The
# attempts, in order, and each is a whole resolution:
#
#   pinned     the task-owned lock, built `--offline --locked`. No resolution
#              happens and nothing is downloaded, so nothing in /app can break
#              or influence it. Every candidate that left Cargo.toml alone -
#              the Oracle included - takes this path and only this path.
#   candidate  the candidate's own lock as the sole seed, completed by ONE
#              resolver run (`cargo fetch`) that can only add packages no
#              lockfile supplied at all - the driver's own dependencies - and
#              then frozen with `--locked`. This is the candidate's own
#              resolution extended, not a merge: nothing from the task-owned
#              lock is in the directory. It is what makes a candidate that
#              legitimately changed its dependencies gradable.
#   fresh      no seed at all: one complete re-resolution of both manifests
#              together, then frozen with `--locked`. Internally consistent by
#              construction, and the last resort for a candidate that changed
#              its dependencies and shipped no usable lockfile.
#
# The resolution is chosen ONCE, before a single line of driver code is
# compiled, by a probe that builds the whole dependency graph and none of the
# drivers. A driver build failure that no lockfile could fix, such as a plain
# E0599 against the candidate's API, therefore never triggers a re-resolution,
# and a target that already compiled is never rebuilt against a different lock.
gate_build_ok=1
conf_build_ok=1

# The resolution search is bounded in aggregate, so it costs a bounded amount
# however many attempts it makes; each driver target is bounded on its own.
RESOLVE_BUDGET=1200
PROBE_MAX=900
DRIVER_BUILD_MAX=900
resolve_deadline=0

# The attribution phase below is entered ONLY when the aggregate conformance
# target failed to compile, and it too is bounded in aggregate: whatever it
# cannot compile inside ATTRIB_BUDGET is reported as the aggregate failure, so
# the fallback can never cost more than a bounded amount and can never turn an
# attributable failure into a timeout.
#
# The arithmetic these two numbers have to fit, against this step's
# `[steps.verifier] timeout_sec = 3600`: the whole verifier phase measures
# about 145 s on the Oracle and about 300 s on a candidate that needs the
# `candidate` resolution, of which the attribution phase itself measured 55 s
# for eighteen groups - one dependency graph is already built and cached, so
# each group costs one 2-4 s recompile of a single crate and its link. 900 s is
# therefore some sixteen times the measured cost, and a run that spends all of
# it still finishes inside a third of the budget. A group that cannot be
# compiled in ATTRIB_BUILD_MAX, or that arrives after the budget is gone, is
# reported as the aggregate failure, so overrunning degrades to aggregate
# reporting instead of costing the scorecard.
ATTRIB_BUDGET=900
ATTRIB_BUILD_MAX=240
attrib_deadline=0

try_resolution() {  # try_resolution <name> <pinned|candidate|none> <complete: yes|no>
    local name="$1" seed="$2" complete="$3" limit rc
    limit=$((resolve_deadline - $(date +%s)))
    [ "$limit" -gt "$PROBE_MAX" ] && limit=$PROBE_MAX
    if [ "$limit" -le 5 ]; then
        note "$name: skipped, the ${RESOLVE_BUDGET}s resolution budget was exhausted"
        return 1
    fi
    # Exactly one lockfile, ever: start from none.
    rm -f -- "$harness/Cargo.lock"
    case "$seed" in
    pinned)
        cp /tests/assets/harness/Cargo.lock.fallback "$harness/Cargo.lock"
        ;;
    candidate)
        # /app is untrusted candidate data: a symlink here must not be followed.
        if [ ! -f "$app/Cargo.lock" ] || [ -L "$app/Cargo.lock" ]; then
            note "$name: skipped, the candidate has no regular Cargo.lock"
            return 1
        fi
        cp -- "$app/Cargo.lock" "$harness/Cargo.lock"
        ;;
    none) ;;
    esac
    if [ "$complete" = yes ]; then
        # One resolver run over the single lockfile that is present, or over no
        # lockfile at all, which also downloads whatever it resolved. No other
        # lockfile is in the directory, so this cannot splice two resolutions.
        ( cd "$harness" && timeout "$limit" cargo fetch ) \
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
    # The probe. `cargo build` compiles every declared dependency of the driver
    # crate plus the candidate crate itself, and - because the harness lib
    # target holds no code - not one line of driver code. So it accepts or
    # rejects the RESOLUTION and nothing else: a driver that does not typecheck
    # against the candidate's API cannot reach it.
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
        # The resolution is frozen from here on. `--locked --offline` on every
        # remaining cargo invocation means no later step can re-resolve or
        # download, so one driver target's compile failure cannot change the
        # dependency graph another target has already been built against.
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

        # Phase 2. One compile error anywhere in the 2000-line driver zeroes
        # the whole `conformance` target, and with it every scored group,
        # because they share ONE compile unit - a candidate that omits a single
        # documented method scores 0 of 18 when only the group that exercises
        # that method should have failed. So when, and only when, the aggregate
        # target fails to compile, each group's cases are compiled on their own
        # below and the failure is attributed to the group it belongs to.
        #
        # This costs nothing on the path every healthy candidate takes: the
        # aggregate target compiles, `attribution` stays `aggregate`, and not
        # one extra cargo invocation runs.
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
# Recorded at the end as well as in resolution.log: `finish` keeps only the
# last 20 kB of harness.log, and a long compile can push its head out.
printf 'driver dependency resolution: %s\n' "$resolution" >> "$log_dir/harness.log"

# ---------------------------------------------------------------------------
# Failure attribution: one compile unit per group, cut from the shipped driver.
#
# The cut is subtractive and it is made from the integrity-checked driver every
# time, never from a previous cut. For a target group it inserts
# `#[cfg(any())]` - an always-false configuration predicate, so the item is
# removed before it is type-checked - ahead of the attribute block of every
# case function belonging to a DIFFERENT group. Nothing else in the file is
# touched: the target group's case bodies and every shared helper in the file
# are compiled from the same bytes as before, so the same cases assert the same
# things and the group's expected case count is unchanged. A helper that does
# not typecheck still fails every group, which is correct - every group depends
# on it.
#
# A case function is recognised only when the prefix before its `__` is a group
# named in groups.txt, and the number of removed cases is checked against
# groups.txt afterwards. If either check does not hold the slice is refused and
# the group is reported as the aggregate failure.
#
# One consequence worth knowing when reading a failure log: a slice carries one
# extra line per removed case, so the line numbers in a per-group compiler error
# are offset from the shipped driver's by the number of cases removed above the
# error. The file name and the source line quoted by rustc are still the
# driver's own.
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
    # `.slice` is not `.rs`, so a partial or refused cut is never a cargo
    # target of its own; it is removed either way.
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
    if [ "$conf_build_ok" -ne 0 ]; then
        # The whole-file target did not compile. Compile THIS group's cases on
        # their own to find out whether the failure belongs to this group.
        local arc=2
        if [ "$attribution" = per-group ]; then
            attrib_compile "$name" "$logf"
            arc=$?
        fi
        case "$arc" in
        0) ;;   # attributed elsewhere: this group's cases compile, so run them
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
if [ "$build_ok" -ne 0 ] || [ "$gate_build_ok" -ne 0 ]; then
    printf 'gate: candidate did not build, or the gate driver did not compile against it (resolution=%s)\n' \
        "$resolution" >> "$gate_log"
    cp "$gate_log" "$gate_case_log" 2>/dev/null || true
    if [ "$build_ok" -ne 0 ]; then
        record "$GATE_GROUP" 1 "build-failed"
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
