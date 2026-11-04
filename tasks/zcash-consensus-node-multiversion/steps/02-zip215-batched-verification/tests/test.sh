#!/usr/bin/env bash
# Verifier for one step of zcash-consensus-node-multiversion.
#
# Per-step copy. Every copy is byte-identical except for the step number in the
# require_step call and the contents of assets/.
#
# Contract, in one place:
#
#   reward = gate x score
#
#     gate  in {0,1}  every GATE group passes completely, or the step is zero.
#                     Gate groups are proof of life plus one deterministic
#                     end-to-end path through SELECTED inherited milestones --
#                     those this step both leans on and can assert
#                     deterministically, not every milestone it inherits.
#                     Gate groups earn nothing; they only veto.
#     score in [0,1]  weighted pass fraction over the SCORED groups ONLY.
#                     Scored weights sum to 100 among themselves and are
#                     normalised here, so they sum to 1.0 as the contract says.
#
#   A no-op therefore scores exactly 0: it preserves inherited behaviour, so
#   gate = 1, and implements no new capability, so score = 0. There is no
#   per-step "no-op ceiling" to compute and no inherited share to subtract.
#
#   Two caveats on that sentence. gate = 1 for a no-op holds only if every gate
#   case really does pass on the PREDECESSOR tree; a gate case that depends on a
#   symbol new at this step makes the gate unpassable and measures nothing. And
#   at step 01 the sentence is simply false: step 01 inherits nothing, so a
#   no-op fails proof of life and reaches 0 by gate veto rather than by
#   score = 0.
#
#   Because a gate failure is all-or-nothing, reward_details.json always NAMES
#   the gate group that vetoed and why. A step reporting 0 without naming the
#   vetoing case is indistinguishable from a candidate that did nothing.
#   * Every group runs even after another group has failed. A group that could
#     not be attempted is recorded as failed with the reason, never skipped.
#   * The verifier always exits 0. Non-zero means "the verifier crashed", and
#     Harbor then skips every remaining step.
#   * Nothing the task ships is fetched at grading time: the drivers, fixtures
#     and specs are baked into the image. Dependencies are not fetched either
#     so long as the candidate leaves the dependency graph alone -- the image
#     carries a Cargo cache pre-populated with this step's exact pinned graph,
#     and cargo is invoked `--offline --locked` against it. The registry is
#     reachable, and `pick_resolution` below falls back towards it only when a
#     candidate's own manifest or lockfile changes stop the cache from
#     satisfying the graph. Which of the four modes was used is recorded in
#     verifier_environment.json; it is reported, not scored.
set -u

TESTS=/tests
ASSETS="$TESTS/assets"
APP=${APP:-/app}
LOGDIR=/logs/verifier
WORKLOG=/tmp/hbv-logs
mkdir -p "$LOGDIR" "$WORKLOG"

# shellcheck source=/dev/null
. "$TESTS/helpers.sh"
require_step "02"

# shellcheck source=/dev/null
. "$ASSETS/config.sh"      # STEP_NN STEP_NAME CARGO_TARGET

export CARGO_TERM_COLOR=never
# Drivers locate their fixture corpus through this variable rather than a
# hardcoded path, so a driver can also be run by hand from a checkout.
export HBV_FIXTURES="$ASSETS/fixtures"
export CARGO_TARGET_DIR="${CARGO_TARGET:-/cache/target}"
export CARGO_NET_OFFLINE=false
export RUSTUP_TOOLCHAIN="${VERIFIER_TOOLCHAIN:-$(cat /etc/harbor-rust-toolchain 2>/dev/null || echo '')}"
[ -n "$RUSTUP_TOOLCHAIN" ] || unset RUSTUP_TOOLCHAIN
JOBS=4

# ---------------------------------------------------------------- bookkeeping
# groups.txt is TSV: name <tab> kind <tab> weight <tab> crate <tab> arg
#
#   kind   gate    proof of life + inherited regression. Binary, earns nothing,
#                  vetoes the whole step. Weight column MUST be 0.
#          scored  this step's new capability + peripheral breadth. Weight is a
#                  positive integer; the scored weights sum to 100.
#
#   crate  a crate name  -> a driver group; `arg` is the exact number of test
#                           cases the shipped driver must execute
#          @build        -> a build group;  `arg` is a cargo subcommand plus a
#                           package selector, e.g. `build -p zebra-chain`
GROUP_ORDER=(); declare -A G_KIND G_WEIGHT G_CRATE G_WANT G_PASS G_DETAIL

load_groups() {
    local name kind weight crate want
    while IFS=$'\t' read -r name kind weight crate want; do
        case "${name:-}" in ''|'#'*) continue;; esac
        # An unrecognised kind is a broken manifest, not a scoring condition.
        # Treating it as `scored` would silently un-gate a regression path, and
        # treating it as `gate` would silently zero the step, so refuse instead.
        case "$kind" in
            gate|scored) ;;
            *) echo "groups.txt: group '$name' has kind '$kind'; expected gate|scored" >&2
               report 0 ;;
        esac
        GROUP_ORDER+=("$name")
        G_KIND[$name]=$kind; G_WEIGHT[$name]=$weight
        G_CRATE[$name]=$crate; G_WANT[$name]=$want
        G_PASS[$name]=0; G_DETAIL[$name]="not run"
    done < "$ASSETS/groups.txt"
}

record() {  # record <name> <pass 0|1> <detail>
    local n=$1
    G_PASS[$n]=$2
    G_DETAIL[$n]=$3
    printf '  [%s] %-34s %s\n' "$([ "$2" = 1 ] && echo PASS || echo FAIL)" "$n" "$3"
}

driver_stem() { printf 'hbv_%s' "${1//-/_}"; }

json_escape() { printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g; s/\t/ /g' | tr -d '\000-\037'; }

write_details() {
    local total=0 earned=0 n first=1
    local gate_total=0 gate_failed=0 gate_pass=1 gate_verdict=
    local -a GATE_FAILED=()

    # The gate: binary over the gate groups, earning nothing.
    for n in "${GROUP_ORDER[@]}"; do
        [ "${G_KIND[$n]}" = gate ] || continue
        gate_total=$(( gate_total + 1 ))
        if [ "${G_PASS[$n]}" != 1 ]; then
            gate_pass=0; gate_failed=$(( gate_failed + 1 )); GATE_FAILED+=("$n")
        fi
    done
    # A step with no gate group at all cannot be vetoed, which would silently
    # un-gate the whole milestone. That is a packaging error, so fail closed.
    if [ "$gate_total" -eq 0 ]; then
        gate_pass=0
        gate_verdict="no gate group is declared in groups.txt -- the step cannot be gated"
    elif [ "$gate_pass" = 1 ]; then
        gate_verdict="all $gate_total gate group(s) passed"
    else
        gate_verdict="VETOED by $gate_failed of $gate_total gate group(s): "
        local g
        for g in "${GATE_FAILED[@]}"; do
            gate_verdict="$gate_verdict[$g: ${G_DETAIL[$g]}] "
        done
    fi

    # The score: weighted pass fraction over the SCORED groups only.
    for n in "${GROUP_ORDER[@]}"; do
        [ "${G_KIND[$n]}" = scored ] || continue
        total=$(( total + ${G_WEIGHT[$n]} ))
        [ "${G_PASS[$n]}" = 1 ] && earned=$(( earned + ${G_WEIGHT[$n]} ))
    done
    SCORE=$(awk -v e="$earned" -v t="$total" 'BEGIN { if (t <= 0) { print "0.000000" } else { printf "%.6f", e / t } }')
    REWARD=$(awk -v g="$gate_pass" -v s="$SCORE" 'BEGIN { printf "%.6f", g * s }')
    {
        printf '{\n  "step": "%s",\n  "step_name": "%s",\n  "reward": %s,\n' \
            "$STEP_NN" "$(json_escape "$STEP_NAME")" "$REWARD"
        printf '  "gate": {"passed": %s, "groups_total": %s, "groups_failed": %s, "failed": [' \
            "$([ "$gate_pass" = 1 ] && echo true || echo false)" "$gate_total" "$gate_failed"
        local gfirst=1 g
        for g in ${GATE_FAILED[@]+"${GATE_FAILED[@]}"}; do
            [ "$gfirst" = 1 ] || printf ', '; gfirst=0
            printf '{"name": "%s", "detail": "%s"}' \
                "$(json_escape "$g")" "$(json_escape "${G_DETAIL[$g]}")"
        done
        printf '], "verdict": "%s"},\n' "$(json_escape "$gate_verdict")"
        printf '  "score": %s,\n  "score_weight_total": %s,\n  "score_weight_earned": %s,\n  "groups": [\n' \
            "$SCORE" "$total" "$earned"
        for n in "${GROUP_ORDER[@]}"; do
            [ "$first" = 1 ] || printf ',\n'; first=0
            # A driver group's `arg` is a case count and belongs in the JSON
            # as a number; a `@build` group's is a whole cargo invocation and
            # has to be a quoted string, or the file would not parse.
            case "${G_WANT[$n]}" in
                ''|*[!0-9]*) ce="\"$(json_escape "${G_WANT[$n]}")\"" ;;
                *)           ce="${G_WANT[$n]}" ;;
            esac
            printf '    {"name": "%s", "kind": "%s", "weight": %s, "crate": "%s", "cases_expected": %s, "passed": %s, "detail": "%s"}' \
                "$(json_escape "$n")" "${G_KIND[$n]}" "${G_WEIGHT[$n]}" "${G_CRATE[$n]}" \
                "$ce" "$([ "${G_PASS[$n]}" = 1 ] && echo true || echo false)" \
                "$(json_escape "${G_DETAIL[$n]}")"
        done
        printf '\n  ]\n}\n'
    } > "$LOGDIR/reward_details.json"
}

finish() {
    write_details
    echo
    # Print the decomposition, never the bare number: a 0 from a vetoing gate
    # and a 0 from an empty implementation mean completely different things.
    local n
    echo "gate groups:"
    for n in "${GROUP_ORDER[@]}"; do
        [ "${G_KIND[$n]}" = gate ] || continue
        printf '  [%s] %s\n' "$([ "${G_PASS[$n]}" = 1 ] && echo PASS || echo VETO)" "$n"
    done
    echo "step $STEP_NN ($STEP_NAME): gate x score = reward"
    echo "  score  = $SCORE  (weighted over scored groups only)"
    echo "  reward = $REWARD"
    grep -o '"verdict": "[^"]*"' "$LOGDIR/reward_details.json" | sed 's/^/  /'
    report "$REWARD"
}

# Anything that kills this script before finish() -- a timeout, an OOM, an
# unexpected `exit` -- still leaves a complete, honest reward_details.json and
# a reward of 0 rather than a missing file Harbor has to guess about.
on_exit() {
    local rc=$?
    [ -f "$LOGDIR/reward.json" ] && return
    echo "VERIFIER TERMINATED before scoring completed (rc=$rc)" >&2
    local n
    for n in "${GROUP_ORDER[@]}"; do
        [ "${G_DETAIL[$n]}" = "not run" ] && G_DETAIL[$n]="verifier terminated (rc=$rc)"
    done
    write_details
    report 0
}
trap on_exit EXIT

# ------------------------------------------------------- 1. asset integrity
echo "=== zcash-consensus-node-multiversion / step $STEP_NN ==="
load_groups
[ "${#GROUP_ORDER[@]}" -gt 0 ] || { echo "no groups defined" >&2; report 0; }

fail_everything() {  # fail_everything <reason>
    local n
    for n in "${GROUP_ORDER[@]}"; do record "$n" 0 "$1"; done
    finish
}

echo "--- asset integrity"
if ! ( cd "$ASSETS" && sha256sum -c --quiet SHA256SUMS ) > "$WORKLOG/assets.log" 2>&1; then
    head -20 "$WORKLOG/assets.log" >&2
    fail_everything "verifier assets failed their checksum manifest"
fi
want_drivers=$(cat "$ASSETS/DRIVER_COUNT")
got_drivers=$(find "$ASSETS/drivers" -maxdepth 1 -name '*.rs' | wc -l)
if [ "$got_drivers" -ne "$want_drivers" ]; then
    fail_everything "driver count $got_drivers != declared $want_drivers"
fi
# every non-build group must own exactly one driver, and every driver must be
# claimed by exactly one group: a silently missing driver would otherwise look
# like an ordinary failure instead of a broken verifier.
claimed=0
for n in "${GROUP_ORDER[@]}"; do
    [ "${G_CRATE[$n]}" = "@build" ] && continue
    claimed=$(( claimed + 1 ))
    [ -f "$ASSETS/drivers/$(driver_stem "$n").rs" ] || \
        fail_everything "group $n has no driver $(driver_stem "$n").rs"
done
[ "$claimed" -eq "$want_drivers" ] || \
    fail_everything "declared $want_drivers drivers but $claimed groups claim one"
echo "    $want_drivers drivers, $(wc -l < "$ASSETS/SHA256SUMS") checksummed assets, OK"

[ -d "$APP" ] || fail_everything "no candidate workspace at $APP"
[ -f "$APP/Cargo.toml" ] || fail_everything "no Cargo.toml at $APP -- not a cargo workspace"

# ---------------------------------------------- 2. choose a dependency resolution
#
# Preference order, and never a mixture:
#   pinned     the candidate left Cargo.lock alone and it agrees with every
#              manifest, so the image's warm cache satisfies it and nothing is
#              fetched.
#   offline    the lockfile and the manifests disagree -- which is true of the
#              upstream tree at some tags, where the checked-in lock carries
#              stale workspace-member versions -- but every *third-party* crate
#              is still in the warm cache, so cargo can re-resolve the path
#              members without reaching the network. This is the Oracle's path
#              at those steps and is exactly as pinned as `pinned` is.
#   locked     the candidate changed its manifest; honour its lockfile but
#              allow the registry to be reached for what the cache lacks.
#   fresh      the candidate changed its manifest and its lockfile disagrees;
#              let cargo re-resolve. Recorded, because it is the only mode in
#              which the graded graph is not the one the task pinned.
RESOLUTION=unknown
declare -a CARGOFLAGS
pick_resolution() {
    local attempt
    for attempt in pinned offline locked fresh; do
        case "$attempt" in
            pinned)  CARGOFLAGS=(--offline --locked);;
            offline) CARGOFLAGS=(--offline);;
            locked)  CARGOFLAGS=(--locked);;
            fresh)   CARGOFLAGS=();;
        esac
        if ( cd "$APP" && cargo metadata --format-version 1 "${CARGOFLAGS[@]}" >/dev/null ) \
                > "$WORKLOG/resolve-$attempt.log" 2>&1; then
            RESOLUTION=$attempt
            echo "--- dependency resolution: $attempt (${CARGOFLAGS[*]:-none})"
            return 0
        fi
    done
    return 1
}
if ! pick_resolution; then
    tail -30 "$WORKLOG/resolve-fresh.log" >&2
    fail_everything "no dependency resolution succeeded: the candidate manifest does not resolve"
fi

# ----------------------------------------------------------- 3. build groups
#
# Build groups are proof of life and are therefore GATE groups: they earn no
# weight and they veto. The group literally named `build` is additionally the
# precondition for the driver groups -- if the crate under test does not
# compile, no driver can run, and each driver group is then recorded as failed
# with that reason rather than skipped, so the details stay honest even though
# the gate has already decided the reward.
BUILD_OK=0
# `arg` for a build group is a whole cargo invocation minus the flags this
# harness adds: a subcommand followed by a package selector, e.g.
#   build -p zebra-chain --tests
#   check --workspace --all-targets
# `check` is used where only type-checking against the candidate's surface is
# being asserted; it still runs every build script, so it still proves the
# native dependencies configure.
run_build_group() {
    local name=$1 sel=${G_WANT[$1]} log="$WORKLOG/build-$1.log"
    # shellcheck disable=SC2086
    if ( cd "$APP" && cargo $sel "${CARGOFLAGS[@]}" --jobs "$JOBS" ) > "$log" 2>&1; then
        record "$name" 1 "cargo $sel ok ($RESOLUTION)"
    else
        local first
        first=$(grep -m1 -E '^error' "$log" | cut -c1-140)
        record "$name" 0 "cargo $sel failed ($RESOLUTION)${first:+ | $first}"
    fi
}

echo "--- build groups"
for n in "${GROUP_ORDER[@]}"; do
    [ "${G_CRATE[$n]}" = "@build" ] || continue
    run_build_group "$n"
done
BUILD_OK=${G_PASS[build]:-0}

# ------------------------------------------------------------- 4. run the groups
#
# Each group is one injected integration test file compiled against the
# candidate's own crates, using only the candidate's own declared dependencies,
# so injecting it changes neither Cargo.toml nor Cargo.lock and `--locked`
# still holds. Every group runs regardless of what any earlier group did.
run_group() {
    local name=$1 crate=${G_CRATE[$1]} want=${G_WANT[$1]} stem
    stem=$(driver_stem "$name")

    if [ "$BUILD_OK" != 1 ]; then
        record "$name" 0 "workspace did not build"; return
    fi
    if [ ! -d "$APP/$crate" ]; then
        record "$name" 0 "crate $crate is absent from the candidate workspace"; return
    fi
    mkdir -p "$APP/$crate/tests"
    cp -f "$ASSETS/drivers/$stem.rs" "$APP/$crate/tests/$stem.rs"

    local log="$WORKLOG/$name.log" executed=0 passed=0 failed=0 summary=
    if ! ( cd "$APP" && cargo test "${CARGOFLAGS[@]}" --jobs "$JOBS" \
                -p "$crate" --test "$stem" -- --test-threads=1 ) > "$log" 2>&1; then
        :   # a non-zero exit is an ordinary group failure; the counts decide
    fi
    executed=$(grep -m1 -oE '^running [0-9]+ test' "$log" | grep -oE '[0-9]+' || true)
    executed=${executed:-0}
    summary=$(grep -m1 -E '^test result:' "$log" || true)
    if [ -n "$summary" ]; then
        passed=$(printf '%s' "$summary" | grep -oE '[0-9]+ passed' | grep -oE '[0-9]+')
        failed=$(printf '%s' "$summary" | grep -oE '[0-9]+ failed' | grep -oE '[0-9]+')
    fi
    passed=${passed:-0}; failed=${failed:-1}

    # The executed-count assertion. A driver that compiled but whose cases were
    # filtered out, renamed away or `#[ignore]`d would otherwise report "0
    # failed" and score. The count is pinned in groups.txt and checked on every
    # single invocation.
    if [ "$executed" -ne "$want" ]; then
        record "$name" 0 "executed=$executed/$want expected -- driver did not run as shipped"
    elif [ "$failed" -ne 0 ] || [ -z "$summary" ]; then
        local firstfail
        firstfail=$(grep -m1 -E '^---- .* stdout ----' -A3 "$log" | tail -2 | tr '\n' ' ' | cut -c1-140)
        record "$name" 0 "executed=$executed passed=$passed failed=$failed${firstfail:+ | $firstfail}"
    else
        record "$name" 1 "executed=$executed passed=$passed"
    fi
}

echo "--- driver groups"
for n in "${GROUP_ORDER[@]}"; do
    [ "${G_CRATE[$n]}" = "@build" ] && continue
    run_group "$n"
done

# The Oracle's resolution must be the pinned one; anything else means the image
# cache and the shipped lockfile have drifted apart. It is not a scoring
# condition -- a candidate is allowed to add dependencies -- but it is recorded
# so a reference run can be checked against it.
printf '{"resolution":"%s","build_ok":%s,"toolchain":"%s"}\n' \
    "$RESOLUTION" "$([ "$BUILD_OK" = 1 ] && echo true || echo false)" \
    "$(rustc --version 2>/dev/null | tr -d '"')" > "$LOGDIR/verifier_environment.json"

finish
