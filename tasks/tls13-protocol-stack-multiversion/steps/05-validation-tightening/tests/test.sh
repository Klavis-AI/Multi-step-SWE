#!/usr/bin/env bash
#
# Verifier for step 05 (05-validation-tightening), rustls 0.21.6.
#
# Contract with harbor:
#   * writes /logs/verifier/reward.json = {"reward": <0..1>} for THIS STEP only
#   * exits 0 always -- a non-zero exit is a crash, and harbor treats a crashed
#     verifier by skipping every remaining step of the ladder, which would turn
#     one broken group into a zero for the whole task
#   * reward = gate x score. `gate` is 1 only if every GATE group passed
#     and 0 otherwise; `score` is the weighted pass fraction over the
#     SCORED groups alone. Tier and weight come from data/groups.tsv, so
#     this file never hard-codes either
#   * on a gate failure reward.json still carries a real 0 and
#     reward_details.json names which gate group vetoed and why
#
# Everything specific to this step lives in /tests/data/. This file is the same
# at all thirteen steps apart from the substituted header constants, so a fix
# here is a fix everywhere.

set -u
export LC_ALL=C LANG=C
export CARGO_TERM_COLOR=never

TESTS=/tests
. "$TESTS/helpers.sh"

STEP="05"
RELEASE="0.21.6"

# Refuse to grade a workspace that was built for a different milestone. This is
# the tripwire for the docker build-context cache: the builder keys a context
# file on (path, size, mtime) and never reads its bytes, so two steps whose
# tests/ trees agree on all three are interchangeable to it. See the Dockerfile.
require_step "$STEP"
harden_reward_dir

WORK="$(mktemp -d /tmp/verify-XXXXXXXX)"
RESULTS="$WORK/results"
: > "$RESULTS"

# Anything the candidate's own code is allowed to write goes here and nowhere
# else. mktemp gives WORK mode 700, so the unprivileged uid cannot even reach a
# subdirectory of it; 711 lets it traverse without being able to list WORK or
# touch the results file, which stays root-owned. Two things need this: the
# BoGo runner's -json-output, and the RSA fixtures the example server reads.
UNPRIV="$WORK/unpriv"
mkdir -p "$UNPRIV"
chmod 711 "$WORK"
chown "$SCORE_UID:$SCORE_GID" "$UNPRIV" 2>/dev/null || true
chmod 700 "$UNPRIV"

# KIND <TAB> GROUP <TAB> WEIGHT. GATE groups carry weight 0 and veto;
# SCORED weights sum to 1.0. See finish().
MANIFEST="$TESTS/data/groups.tsv"
DEADLINE=$(( $(date +%s) + ${VERIFIER_BUDGET_SEC:-6300} ))

log() { printf '%s\n' "$*" >&2; }
remaining() { echo $(( DEADLINE - $(date +%s) )); }

# --------------------------------------------------------------------------
# Group bookkeeping.
#
# A group is recorded exactly once. A group in the manifest that is never
# recorded scores zero and is reported by name -- silence must never be
# mistaken for success, and "the stage that would have recorded it crashed" is
# precisely when a group is most likely to be missing.
# --------------------------------------------------------------------------
record() {
    local g="$1" outcome="$2" note="${3-}"
    # The note travels into a tab-separated scratch file and then into JSON, so
    # flatten anything that would split a field or a line. A diagnostic that
    # corrupts reward_details.json is worse than a terse one, and on a gate
    # failure this string is the only thing that says which case vetoed.
    # tr twice: flatten the field/line separators to spaces, then drop any
    # remaining control byte outright (a stray ESC would still be invalid JSON).
    note="$(printf '%s' "$note" | tr '\t\n\r' '   ' | tr -d '\000-\037\177')"
    if grep -q "^$g " "$RESULTS" 2>/dev/null; then
        log "[verifier-bug] group recorded twice: $g"
        return 0
    fi
    printf '%s %s %s\n' "$g" "$outcome" "$note" >> "$RESULTS"
    printf '[group] %-52s %-4s %s\n' "$g" "$outcome" "$note" >&2
}
pass() { record "$1" pass "${2-}"; }
fail() { record "$1" fail "${2-}"; }

# Record every not-yet-recorded manifest group as a failure. Used when a
# precondition collapses (the candidate does not build, the shim is missing)
# and the remaining stages cannot produce a meaningful answer.
fail_everything() {
    local reason="$1" g
    while IFS=$'\t' read -r _kind g _w; do
        [ -n "$g" ] || continue
        grep -q "^$g " "$RESULTS" 2>/dev/null || record "$g" fail "$reason"
    done < "$MANIFEST"
}

FINISHED=0

# --------------------------------------------------------------------------
# reward = gate x score
#
#   gate   in {0,1}  every GATE group passes, or the step scores zero
#   score  in [0,1]  weighted pass fraction over the SCORED groups only
#
# The gate is the step's inherited regression -- one deterministic end-to-end
# path per milestone carried forward -- plus proof of life. It earns nothing;
# it only vetoes. The scored groups are this step's own new capability and
# their weights sum to 1.0 among themselves, so a continuation that preserves
# the inherited work and implements nothing new scores exactly 0: gate 1,
# score 0.
#
# The cliff is deliberate and it is why every gate case has to be provably
# deterministic. It is also why a gate failure is never reported as a bare
# zero: reward_details.json names the gate group that vetoed and the diagnostic
# it recorded, because otherwise a step zeroed by one flaky regression path is
# indistinguishable from a candidate that did nothing.
# --------------------------------------------------------------------------
finish() {
    [ "$FINISHED" = 1 ] && return 0
    FINISHED=1

    local g kind weight outcome note
    local n_gate=0 p_gate=0 n_scored=0 p_scored=0
    local missing="" gate_failed=""

    : > "$WORK/final"
    while IFS=$'\t' read -r kind g weight; do
        [ -n "$g" ] || continue
        outcome="$(awk -v k="$g" '$1==k {print $2; exit}' "$RESULTS")"
        note="$(awk -v k="$g" '$1==k {$1="";$2="";sub(/^  /,"");print;exit}' "$RESULTS")"
        if [ -z "$outcome" ]; then
            outcome=fail
            note="group never executed"
            missing="$missing $g"
        fi
        case "$kind" in
          GATE)
            n_gate=$(( n_gate + 1 ))
            if [ "$outcome" = pass ]; then
                p_gate=$(( p_gate + 1 ))
            else
                gate_failed="$gate_failed $g"
            fi ;;
          SCORED)
            n_scored=$(( n_scored + 1 ))
            [ "$outcome" = pass ] && p_scored=$(( p_scored + 1 )) ;;
          *)
            # A manifest row the scorer cannot classify is a corrupt manifest,
            # and the safe reading of a corrupt manifest is that the step did
            # not pass. Counting it as a failed GATE row is what makes that
            # happen: it fails closed, and it is carried into $WORK/final as
            # GATE so the JSON below names it under gate.failed like any other
            # veto rather than dropping it silently.
            log "[verifier-bug] group $g has unknown kind '$kind'"
            kind=GATE
            outcome=fail
            note="manifest declares an unknown kind for this group"
            n_gate=$(( n_gate + 1 ))
            gate_failed="$gate_failed $g" ;;
        esac
        printf '%s\t%s\t%s\t%s\t%s\n' "$kind" "$g" "$weight" "$outcome" "$note" \
            >> "$WORK/final"
    done < "$MANIFEST"

    if [ -n "$missing" ]; then
        log "[verifier] groups never executed (scored 0):$missing"
    fi

    # The cross-check in the other direction. finish() scores the manifest; the
    # BoGo stage iterates data/bogo-groups/ and step-checks.sh names its own
    # groups, so a case list added under data/bogo-groups/ without a matching
    # line in the manifest -- or a step-checks.sh group whose `# GROUP:` comment
    # was not updated -- would be executed, recorded, and then scored by nobody.
    # The manifest is derived from exactly those two sources at generation time,
    # so the two cannot drift in a generated tree; this catches a hand-edited
    # one. It changes no score: the manifest is the denominator by definition.
    # It only refuses to let the drift be silent, which is the whole failure
    # mode -- an unscored group looks identical to a group that does not exist.
    local unscored
    unscored="$(cut -d' ' -f1 "$RESULTS" | sort -u \
                | comm -23 - <(cut -f2 "$MANIFEST" | sort -u))"
    if [ -n "$unscored" ]; then
        log "[verifier-bug] recorded but not in the manifest, so scored by nobody: $(printf '%s' "$unscored" | tr '\n' ' ')"
    fi
    if [ "$n_scored" -eq 0 ]; then
        log "[verifier-bug] manifest declares no scored group"
        report 0
        exit 0
    fi

    local gate score reward
    if [ "$n_gate" -eq "$p_gate" ]; then gate=1; else gate=0; fi
    score="$(awk -F'\t' 'BEGIN{e=0;t=0}
        $1=="SCORED"{t+=$3; if($4=="pass") e+=$3}
        END{ if (t<=0) printf "0.000000"; else printf "%.6f", e/t }' "$WORK/final")"
    reward="$(awk -v g="$gate" -v s="$score" 'BEGIN{printf "%.6f", g*s}')"

    log "[verifier] step $STEP ($RELEASE): gate $p_gate/$n_gate -> $gate   scored $p_scored/$n_scored -> $score   reward $reward"
    if [ "$gate" = 0 ]; then
        log "[verifier] GATE FAILED, step scores 0 regardless of new capability. Vetoing group(s):$gate_failed"
        local f
        for f in $gate_failed; do
            log "[verifier]   $f: $(awk -F'\t' -v k="$f" '$2==k{print $5; exit}' "$WORK/final")"
        done
    fi

    # Diagnostic authority. harbor reads reward.json and nothing else; this
    # file is what a human reads to see why a step scored what it did -- and,
    # on a gate failure, which case vetoed. A step reporting 0 without naming
    # the vetoing case cannot be told apart from a candidate that did nothing,
    # so `gate.failed` is populated whenever the verdict is `fail`.
    {
        printf '{\n'
        printf '  "step": "%s",\n  "release": "%s",\n' "$STEP" "$RELEASE"
        printf '  "reward_model": "gate x score",\n'
        printf '  "gate": {\n'
        printf '    "verdict": "%s",\n' "$([ "$gate" = 1 ] && echo pass || echo fail)"
        printf '    "value": %s,\n' "$gate"
        printf '    "total": %d,\n    "passed": %d,\n' "$n_gate" "$p_gate"
        printf '    "failed": [\n'
        awk -F'\t' 'BEGIN{n=0}
            $1=="GATE" && $4!="pass" {
                if (n++) printf ",\n"
                why=$5; gsub(/\\/,"\\\\\\\\",why); gsub(/"/,"\\\"",why)
                printf "      {\"group\": \"%s\", \"why\": \"%s\"}", $2, why
            }
            END{ if (n) printf "\n" }' "$WORK/final"
        printf '    ]\n  },\n'
        printf '  "scored": {"total": %d, "passed": %d},\n' "$n_scored" "$p_scored"
        printf '  "score": %s,\n' "$score"
        printf '  "reward": %s,\n' "$reward"
        printf '  "groups": {\n'
        awk -F'\t' 'BEGIN{n=0}
            {
                if (n++) printf ",\n"
                why=$5; gsub(/\\/,"\\\\\\\\",why); gsub(/"/,"\\\"",why)
                printf "    \"%s\": {\"kind\": \"%s\", \"weight\": %s, \"outcome\": \"%s\", \"note\": \"%s\"}",
                       $2, $1, ($1=="GATE" ? "0" : $3), $4, why
            }
            END{ if (n) printf "\n" }' "$WORK/final"
        printf '  }\n}\n'
    } > /logs/verifier/reward_details.json 2>/dev/null || true
    chmod 644 /logs/verifier/reward_details.json 2>/dev/null || true

    report "$reward"
}

# If anything below kills the shell -- an unset variable, a segfault in a tool,
# the step timeout -- still write a reward file rather than leaving harbor to
# infer one from a missing file.
on_exit() {
    local rc=$?
    if [ "$FINISHED" != 1 ]; then
        log "[verifier] terminated early (rc=$rc); unrun groups score 0"
        fail_everything "verifier terminated early"
        finish
    fi
    rm -rf "$WORK" 2>/dev/null || true
    exit 0
}
trap on_exit EXIT

[ -r "$MANIFEST" ] || { log "[verifier-bug] no group manifest"; report 0; exit 0; }
log "[verifier] step $STEP ($RELEASE), $(wc -l < "$MANIFEST") groups, budget $(remaining)s"

# --------------------------------------------------------------------------
# Stage A -- build the candidate workspace.
#
# The dependency graph is already in the image's registry cache, keyed to this
# step's frozen Cargo.lock, so the offline attempt is the one that normally
# succeeds. The single online retry exists because a candidate is allowed to add
# a dependency; which path was taken is reported, and neither is penalised.
# --------------------------------------------------------------------------
give_app_to_unprivileged

# One cargo invocation per line.
#
# The file holds two build lines -- the BoGo shim and the example binaries the
# probes drive -- and they are not the same build. Cargo applies
# `--features`/`--no-default-features` to the whole invocation and unifies
# features across every package it resolves, so running them together silently
# merges the two feature sets. At steps 08 and 09 that would undo the shim
# line's own `--no-default-features`; at step 10 it is worse, because
# upstream's `bogo/Cargo.toml` asks rustls for `fips` and a unified build hands
# the probe binaries a FIPS-only default provider, so inherited groups such as
# `inherited-sni-ip-literal-accepted` would see `alert:fatal handshake_failure`
# for a reason that has nothing to do with either the candidate or what those
# groups assert.
#
# Built separately, each artifact gets the feature set its line names. rustls
# compiles twice at a step whose lines disagree, which costs about a minute.
BUILD_LOG="$WORK/build.log"
build_mode=none

# Lint levels are capped for the whole workspace, and the same cap is set in
# the agent image, so the candidate and the verifier compile under identical
# flags. The ladder spans seven years of releases whose crate roots carry
# `#![deny(...)]` sets written for the compiler of their era; this image's rustc
# knows lints those authors never saw. At 0.19.0 `unused_qualifications` alone
# fires fourteen times and upstream's own unmodified source fails to compile.
# Without the cap the first thing every step would grade is whether the
# candidate cleared someone else's lint debt, which is not what any group here
# asserts. This changes no pass condition: it is a compiler diagnostic level,
# and every group is decided by observed wire behaviour after the build.
BUILD_RUSTFLAGS="--cap-lints=allow"

# Each line of build-packages.txt is one required build, written as one or
# more package specs joined by " || ". The line succeeds if *any* of them
# builds; the first is this step's own spec and the rest are the specs earlier
# steps were built with, newest first.
#
# The fallbacks keep layout out of the gate. gate-proof-of-life is a GATE
# group, and the gate does not grade a migration: a release's API, default or
# *layout* change belongs to the new-capability tier, which scores observable
# behaviour, never layout or metadata. Four steps on this ladder rename a
# crate, move the shim, or change its feature set. With only this step's spec
# the line is unsatisfiable on the previous step's finished workspace, which
# has to pass the gate -- so a candidate that preserved every inherited
# behaviour would be vetoed for not yet having done this step's packaging work,
# and the step would report a gate failure where it should report a clean 0.
#
# It costs no coverage. Nothing scored here reads the build line: every scored
# group at those four steps is a wire observable or a BoGo delta, both of which
# need the new behaviour whichever crate name produced the binary.
#
# Offline over every alternative first, then online over every alternative, so
# the usual case -- this step's own spec, cached -- is one cargo invocation and
# the network is touched only when nothing builds without it.
try_build_line() {
    local line="$1" mode rest spec
    for mode in offline online; do
        rest="$line"
        while [ -n "$rest" ]; do
            case "$rest" in
                *" || "*) spec="${rest%% || *}"; rest="${rest#* || }" ;;
                *)        spec="$rest";          rest="" ;;
            esac
            if [ "$mode" = offline ]; then
                log "[verifier] building: cargo build --locked $spec"
                if run_unprivileged env CARGO_NET_OFFLINE=true CARGO_BUILD_JOBS=4 \
                        RUSTFLAGS="$BUILD_RUSTFLAGS" \
                        cargo build --locked $spec >> "$BUILD_LOG" 2>&1; then
                    return 0
                fi
            else
                log "[verifier] offline build failed; retrying with the network: cargo build $spec"
                if run_unprivileged env CARGO_BUILD_JOBS=4 \
                        RUSTFLAGS="$BUILD_RUSTFLAGS" \
                        cargo build $spec >> "$BUILD_LOG" 2>&1; then
                    build_mode=online
                    return 0
                fi
            fi
        done
    done
    return 1
}

build_mode=offline
: > "$BUILD_LOG"
# fd 3, so nothing cargo runs can eat the remaining build lines.
while IFS= read -r BUILD_PKGS <&3; do
    [ -n "$BUILD_PKGS" ] || continue
    if ! try_build_line "$BUILD_PKGS"; then
        build_mode=none
        break
    fi
done 3< "$TESTS/data/build-packages.txt"

# gate-proof-of-life is the one gate group with no wire observable of its own:
# the candidate builds and the artifact's binaries are there to be started. It
# is the whole of step 01's gate and the first member of every later step's,
# and it is recorded here rather than inferred from the other groups, so that a
# tree that does not build reports a named gate veto instead of a bare zero.
if [ "$build_mode" = none ]; then
    log "[verifier] candidate workspace does not build; last 40 lines:"
    tail -40 "$BUILD_LOG" >&2
    fail "gate-proof-of-life" "workspace does not build: $(tail -1 "$BUILD_LOG" 2>/dev/null | tr -d '\t')"
    fail_everything "workspace does not build"
    finish
    exit 0
fi
log "[verifier] build ok ($build_mode), $(remaining)s left"

# shim-path.txt is a candidate list for the same reason the build line carries
# alternatives: this step's expected location first, then the locations earlier
# steps put the shim at. First executable wins. Where the shim lives is layout,
# and the gate does not grade layout.
SHIM=""
while IFS= read -r shim_cand; do
    [ -n "$shim_cand" ] || continue
    if [ -x "/app/$shim_cand" ]; then SHIM="/app/$shim_cand"; break; fi
done < "$TESTS/data/shim-path.txt"
if [ -z "$SHIM" ]; then
    shim_cands="$(tr '\n' ' ' < "$TESTS/data/shim-path.txt" | sed 's/  */ /g; s/ $//')"
    log "[verifier] build succeeded but no executable BoGo shim at any of: $shim_cands"
    fail "gate-proof-of-life" "workspace builds but no executable shim at any of: $shim_cands"
    fail_everything "shim binary absent"
    finish
    exit 0
fi
pass "gate-proof-of-life" "workspace builds ($build_mode) and the shim is executable at $SHIM"

# --------------------------------------------------------------------------
# Stage B -- BoGo conformance.
#
# Runner binary, key fixtures, test-ca and config.json are all verifier-owned
# and frozen at this step's upstream pin. config.json in particular carries
# DisabledTests -- the list of cases that need not pass -- so taking it from
# /app would let a candidate switch off any case it failed. It also carries
# TestLocalErrorMap, whose values are the alert the *runner* expects to see on
# the wire for a handful of cases. ErrorMap and TestErrorMap (which would
# compare implementation error strings) are stripped from it, and where a
# map-pinned case would otherwise be in a scored list it is dropped from that
# list with the reason recorded in data/bogo-excluded.txt. Whatever is in the
# candidate's bogo/ directory is ignored.
#
# Staging location is not a free choice. The shim resolves
# ../../../../../test-ca/rsa-2048/ca.cert against the runner's cwd, which it
# inherits, so the checkout must sit at exactly /app/bogo/bogo -- upstream's own
# layout. Staged anywhere else, roughly two dozen certificate cases fail as shim
# panics that look like candidate defects. Staging happens after the build, so
# the extra directory is never visible to cargo.
#
# test-ca/ is read by the shim from the workspace root, so the verifier replaces
# it with its frozen copy: it is a fixture, and a candidate must not be able to
# change the certificates its own grading uses.
#
# The runner is run once and writes structured per-case results with
# -json-output. Every BoGo group is then scored from that single outcome set,
# which keeps a dozen groups to one run of a few seconds.
# --------------------------------------------------------------------------
# In the unprivileged scratch, not WORK: the runner is launched through
# run_unprivileged and cannot write into a root-owned 700 directory.
BOGO_JSON="$UNPRIV/bogo.json"
BOGO_RAN=0

if [ "$(remaining)" -lt 300 ]; then
    log "[verifier] insufficient budget for the BoGo run"
else
    STAGE=/app/bogo/bogo
    rm -rf "$STAGE"
    mkdir -p /app/bogo
    cp -a /opt/bogo/checkout "$STAGE"
    # Upstream's runme does this copy at every level. It is load-bearing at
    # level 01, where the runner's cwd is the checkout root; harmless later.
    cp -a "$TESTS/data/bogo-keys/." "$STAGE/"
    cp "$TESTS/data/config.json" "$STAGE/config.json"
    rm -rf /app/test-ca
    cp -a "$TESTS/data/test-ca" /app/test-ca
    chown -R "$SCORE_UID:$SCORE_GID" "$STAGE" /app/test-ca 2>/dev/null || true

    RUNNER_DIR="$STAGE/$(cat "$TESTS/data/runner-subdir.txt")"
    log "[verifier] running BoGo from $RUNNER_DIR ($(remaining)s left)"
    ( cd "$RUNNER_DIR" && \
      # -loose-errors is the other half of dropping the error maps from
      # config.json (see the Stage B header). runner.go's translateExpectedError
      # consults TestErrorMap first and only reaches the loose branch when the
      # test has no mapping -- so the flag alone would change nothing, and
      # stripping the maps alone would leave every failing case demanding
      # BoringSSL's own error string, which no rustls ever emits.
      # Together they mean: a case that must fail must still fail, and the
      # words it fails with are not graded.
      #
      # BOGO_SHIM_PROVIDER: from level 10 the shim is its own crate that reads
      # this variable and defaults to aws-lc-rs when it is unset, while
      # config.json is preprocessed with -DRING and every scored case in this
      # package was measured with ring. The runner passes its environment
      # through to the shim it spawns, so setting it here is what makes the
      # graded binary the one the scoring data describes. Any case list frozen
      # under ring can drift against an aws-lc-rs shim: upstream disables some
      # cases on the aws-lc-rs side only. Levels 01-09 build a shim that never
      # reads it; the variable is inert there.
      #
      # -test.timeout is the Go test binary's own deadline and it covers the
      # WHOLE run, not one case: runner_test.go is a single TestAll that calls
      # runner.go's main(), so the testing package arms its watchdog once, for
      # everything the runner does. With a short deadline, a shim slower than
      # the one the case lists were measured against does not lose a case, it
      # loses the run -- the binary panics before it writes -json-output, no
      # result set is parsed, and every BoGo group at this step fails together.
      # So it is set well above the step's whole budget, and the outer
      # `timeout "$(remaining)"` is the bound that applies.
      # Per-case protection is the runner's own -idle-timeout, left at its
      # default of 15 seconds per read or write to the shim; TEST_INTERFACE.md
      # sec 2 and sec 5 describe exactly this.
      run_unprivileged env BOGO_SHIM_PROVIDER=ring \
          timeout "$(remaining)" /opt/bogo/runner.test \
          -shim-path "$SHIM" \
          -shim-config "$STAGE/config.json" \
          -json-output "$BOGO_JSON" \
          -pipe -allow-unimplemented -loose-errors -test.timeout 120m ) \
      > "$WORK/bogo.out" 2>&1
    # The runner exits non-zero whenever any case fails, including cases this
    # step does not require, so its status is deliberately not consulted. What
    # matters is whether it produced a parseable result set.
    if [ -s "$BOGO_JSON" ] && \
       python3 "$TESTS/bin/bogo-outcomes.py" "$BOGO_JSON" > "$WORK/bogo.outcomes" 2>"$WORK/bogo.parse.err"; then
        BOGO_RAN=1
        log "[verifier] BoGo: $(awk '$1=="PASS"' "$WORK/bogo.outcomes" | wc -l) passed, $(awk '$1=="FAIL"' "$WORK/bogo.outcomes" | wc -l) failed, $(awk '$1=="SKIP"' "$WORK/bogo.outcomes" | wc -l) skipped, $(wc -l < "$WORK/bogo.outcomes") total"
    else
        log "[verifier] BoGo produced no usable result set; last 30 lines of its output:"
        tail -30 "$WORK/bogo.out" >&2
        cat "$WORK/bogo.parse.err" >&2 2>/dev/null || true
    fi
fi

if [ "$BOGO_RAN" = 1 ]; then
    awk '$1=="PASS" {print $2}' "$WORK/bogo.outcomes" | sort -u > "$WORK/bogo.passed"
    awk '{print $2}' "$WORK/bogo.outcomes" | sort -u > "$WORK/bogo.seen"
else
    : > "$WORK/bogo.passed"; : > "$WORK/bogo.seen"
fi

if [ "$BOGO_RAN" = 1 ]; then
    awk '$1=="SKIP" {print $2}' "$WORK/bogo.outcomes" | sort -u > "$WORK/bogo.skipped"
else
    : > "$WORK/bogo.skipped"
fi

# score_bogo_group <group>
#
# A group is described by up to two frozen case lists in data/bogo-groups/:
#
#   <group>.cases  every listed case must be reported PASS
#   <group>.skips  every listed case must be reported SKIP
#
# The second kind exists because not every step on this ladder ADDS a
# behaviour. Where a release deliberately withdraws one -- 0.22.0 removing
# certificate timestamps delivered by TLS extension is the clearest example --
# the shim can no longer be configured to attempt those cases and answers "not
# implemented", which the runner records as SKIP. Asserting SKIP is a positive,
# specific claim about the new tree. Asserting merely "does not pass" would not
# be: a broken implementation also does not pass.
#
# A step that has a .skips group is meant to pair it with PASS-requiring
# groups, so that a shim which declined everything would satisfy the withdrawal
# group and fail the rest; withdrawal is never meant to be scoreable on its
# own. Where a step ships only .skips groups -- step 06 is the one -- that
# pairing is absent and nothing in this tier catches a shim that declines more
# than the withdrawal, which is why the rest of that step's score rests on its
# out-of-band groups.
#
# A listed case the runner never emitted is a verifier defect, not a candidate
# failure: the lists were frozen from five runs of this step's own Oracle tree,
# so an absent name means the shipped config or runner disagrees with the frozen
# list. That fails the group loudly and says so, rather than silently counting
# as "not failed".
score_bogo_group() {
    local g="$1"
    local cf="$TESTS/data/bogo-groups/$g.cases"
    local sf="$TESTS/data/bogo-groups/$g.skips"
    [ -f "$cf" ] || cf=""
    [ -f "$sf" ] || sf=""
    if [ -z "$cf" ] && [ -z "$sf" ]; then
        fail "$g" "verifier-defect: no case list for group"; return
    fi
    if [ "$BOGO_RAN" != 1 ]; then fail "$g" "BoGo did not run"; return; fi

    local absent n_absent bad n_bad n_req=0 parts=""
    absent="$( { [ -n "$cf" ] && cat "$cf"; [ -n "$sf" ] && cat "$sf"; } \
               | sort -u | comm -23 - "$WORK/bogo.seen" )"
    n_absent="$(printf '%s' "$absent" | grep -c . || true)"
    if [ "$n_absent" -gt 0 ]; then
        fail "$g" "verifier-defect: $n_absent case(s) absent from run: $(printf '%s' "$absent" | head -3 | tr '\n' ' ')"
        return
    fi

    if [ -n "$cf" ]; then
        bad="$(comm -23 <(sort -u "$cf") "$WORK/bogo.passed")"
        n_bad="$(printf '%s' "$bad" | grep -c . || true)"
        n_req=$(( n_req + $(sort -u "$cf" | wc -l) ))
        if [ "$n_bad" -gt 0 ]; then
            fail "$g" "$n_bad not passing: $(printf '%s' "$bad" | head -3 | tr '\n' ' ')"
            return
        fi
        parts="$(sort -u "$cf" | wc -l) must-pass"
    fi
    if [ -n "$sf" ]; then
        bad="$(comm -23 <(sort -u "$sf") "$WORK/bogo.skipped")"
        n_bad="$(printf '%s' "$bad" | grep -c . || true)"
        n_req=$(( n_req + $(sort -u "$sf" | wc -l) ))
        if [ "$n_bad" -gt 0 ]; then
            fail "$g" "$n_bad not declined (capability should be absent): $(printf '%s' "$bad" | head -3 | tr '\n' ' ')"
            return
        fi
        parts="${parts:+$parts, }$(sort -u "$sf" | wc -l) must-decline"
    fi
    pass "$g" "$parts"
}

# Iterated in THIS shell, not through a pipeline: a `... | while read` loop runs
# its body in a subshell, and every record() call inside it would be discarded.
BOGO_GROUP_NAMES="$(ls "$TESTS"/data/bogo-groups/ 2>/dev/null \
    | sed -n 's/\.\(cases\|skips\)$//p' | sort -u)"
for g in $BOGO_GROUP_NAMES; do
    score_bogo_group "$g"
done

# There is no all-or-nothing BoGo gate group. The BoGo groups above already
# partition the frozen required-pass set -- however many of them this step
# happens to have -- so a gate over that whole set would score every case a
# second time at a different weight.

# --------------------------------------------------------------------------
# Stage C -- everything that is not BoGo: the ClientHello wire observation and
# the OpenSSL differential. Defined per step, because which observables exist
# and what they should say is exactly what changes along the ladder.
# --------------------------------------------------------------------------
# The OpenSSL differential driver and the ClientHello observer are shared by
# every step; only the assertions differ, and those live in step-checks.sh.
. "$TESTS/bin/differential.sh"
. "$TESTS/bin/checks-lib.sh"

# Upstream renamed the RSA fixture directory from `rsa` to `rsa-2048` when it
# replaced the shell PKI generator with the Rust one, so the path is resolved
# rather than hardcoded. Resolved once, here, so that thirteen step-checks files
# do not each have to get it right.
CA=""
for d in "$TESTS/data/test-ca/rsa-2048" "$TESTS/data/test-ca/rsa"; do
    [ -d "$d" ] && { CA="$d"; break; }
done
if [ -n "$CA" ]; then
    # The probes run the candidate's example server unprivileged, and it has to
    # read end.rsa -- a private key, mode 600 root:root, on a read-only mount.
    # So the fixtures are copied into the unprivileged scratch and handed to
    # that uid. The originals stay untouched under /tests, and the copy is
    # discarded with WORK, so nothing the candidate can write survives the run.
    mkdir -p "$UNPRIV/pki"
    cp -a "$CA/." "$UNPRIV/pki/"
    chown -R "$SCORE_UID:$SCORE_GID" "$UNPRIV/pki" 2>/dev/null || true
    CA="$UNPRIV/pki"
else
    log "[verifier] no RSA test-ca directory found; differential groups will fail"
fi
export CA

# The same rewrite dropped `end.rsa`. The old shell generator wrote the end
# entity's key twice -- `end.key` in PKCS#8 and `end.rsa` in the traditional
# RSA encoding -- and the Rust generator writes only `end.key`, so from step 09
# on only `end.key` exists. `end.rsa` stays first so the earlier steps hand
# their servers the traditional encoding; both encodings parse in
# rustls-pemfile and in `openssl s_server -key`, so the fallback changes
# nothing observable where it applies.
END_KEY=""
for k in "$CA/end.rsa" "$CA/end.key"; do
    [ -r "$k" ] && { END_KEY="$k"; break; }
done
[ -n "$END_KEY" ] || log "[verifier] no end-entity private key under $CA; server-side groups will fail"
export END_KEY

if [ -r "$TESTS/data/step-checks.sh" ]; then
    . "$TESTS/data/step-checks.sh"
    if declare -F step_extra_checks > /dev/null; then
        step_extra_checks || log "[verifier] step_extra_checks returned non-zero; unrecorded groups score 0"
    fi
fi

# The probe server outlives individual groups so that several of them can share
# one handshake; nothing after this point needs it, and a stray listener would
# outlive the verifier.
declare -F checks_lib_cleanup > /dev/null && checks_lib_cleanup

finish
exit 0
