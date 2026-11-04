#!/usr/bin/env bash
# Verifier for one step of the vortex-columnar-bit-kernels ladder.
#
# Scoring: reward = correctness x mean_f( ln(max(1, speedup_f)) ) over the
# functions in this step's scope, where speedup_f = t_base(f) / t_candidate(f).
# Both sides are measured in this container, on the same cores, from binaries
# built here from the two trees: /base is the ladder's L0, rebuilt from the pin
# with every one of the seven stages removed, and /app is the candidate's tree.
# The denominator is therefore self-calibrating and the candidate cannot touch
# it.
#
# Identical in every step. What differs is SCOPE.step<NN> (which rows are scored
# for this step) and SHA256SUMS.step<NN> (its binding), both named per step so
# BuildKit's (path, size, mtime) context dedup cannot serve a sibling's copy.
set -u

. /tests/helpers.sh
harden_reward_dir

export PATH=/usr/local/cargo/bin:$PATH
export CARGO_HOME=/usr/local/cargo
export RUSTUP_HOME=/usr/local/rustup
export CARGO_NET_OFFLINE=true
export CARGO_BUILD_JOBS=4

SEED=20260923
REPS=9

log() { printf '%s\n' "$*" >&2; }

# ---------------------------------------------------------------- step binding
STEP=$(ls /tests/SCOPE.step* 2>/dev/null | head -n1 | sed 's#.*SCOPE\.step##')
[ -n "${STEP:-}" ] || fail "no SCOPE.step<NN> file in /tests"
require_step "$STEP"

( cd /tests && sha256sum -c "SHA256SUMS.step${STEP}" > /dev/null 2>&1 ) \
    || fail "per-step data file digest mismatch for step ${STEP}"

SCOPE=/tests/SCOPE.step${STEP}
NROWS=$(grep -c '^[^#[:space:]]' "$SCOPE" || true)
[ "${NROWS:-0}" -ge 1 ] || fail "SCOPE.step${STEP} lists no functions"
log "step ${STEP}: ${NROWS} scored functions"

# ------------------------------------------------------- capability envelope
# The envelope has two halves and they answer two different questions. What
# separates them is *which* statement `correctness` is: "this tree behaves like
# the tree it started from". A tree that changed or added nothing
# satisfies that by definition, and no envelope question can change it.
#
#   host     -- does this machine have the features the envelope thinks it has?
#               Nothing about the candidate, and nothing it can influence: it is
#               an environment precondition, asserted for every tree alike.
#   candidate-- does the candidate's own code stay inside the envelope?
#               Scoped twice, and both scopes matter (taskgen section 6):
#                 * to the STEP -- /app is cumulative, so a gate reading the
#                   whole tree charges a late step for early work;
#                 * to what the CANDIDATE CHANGED OR ADDED -- a tree that
#                   changed and added nothing cannot be out of envelope, and
#                   saying that it is, is the defect this gate exists to stop.
#               The unchanged case therefore passes vacuously *and says so out
#               loud*: a gate that never examined anything reads exactly like a
#               clean pass.

# ---- host precondition -----------------------------------------------------
# On a host without AVX2 or BMI2 nothing is selected and every step would read
# ~1.00x -- a silent collapse that looks like a candidate who did nothing.
# The envelope is data, not prose: tests/ISA names what the oracles were written
# against, and the check reads it rather than restating it, so the file cannot
# drift away from what is enforced.
ISA_REQUIRED=$(sed -n 's/^required:[[:space:]]*//p' /tests/ISA)
ISA_ABSENT=$(sed -n 's/^absent:[[:space:]]*//p' /tests/ISA)
ISA_WIDTH=$(sed -n 's/^width:[[:space:]]*//p' /tests/ISA)
ISA_THREADS=$(sed -n 's/^threads:[[:space:]]*//p' /tests/ISA)
[ -n "$ISA_REQUIRED" ] || fail "tests/ISA names no required CPU features"
[ -n "$ISA_WIDTH" ] || fail "tests/ISA names no register width"
[ -n "$ISA_THREADS" ] || fail "tests/ISA names no thread count"
ISA_N=0
for feat in $ISA_REQUIRED; do
    grep -qw "$feat" /proc/cpuinfo \
        || fail "grading host lacks ${feat}, so this step's kernels are never selected"
    ISA_N=$((ISA_N + 1))
done
for feat in $ISA_ABSENT; do
    if grep -qw "$feat" /proc/cpuinfo; then
        log "NOTE: host provides ${feat}; it is outside the envelope this task pins"
    fi
done
log "envelope/host: ${ISA_N} required feature(s) present, width ${ISA_WIDTH}, threads ${ISA_THREADS}"

# ---- candidate-scoped enforcement ------------------------------------------
# The check has two scopes, because a candidate's kernel can arrive two ways:
#
#   changed -- this step's own files, from tests/ENVELOPE.step<NN>: the files
#              the step's oracle patch touches, derived from the patch rather
#              than written by hand, restricted to the ones the candidate
#              actually changed. The rest are reported as unchanged.
#   added   -- source files the candidate adds: anything under a `src/` tree of
#              /app/vortex that /base does not have. A kernel split into a new
#              module and merely *called* from an owned file is measured
#              exactly like one written into it, so the new module is
#              inspected too. Added `benches/`, `tests/` and `examples/` files
#              are not linked into the library the probe measures, and are not
#              scanned; a *changed* file from the step's own list is scanned
#              wherever it lives, because that list already includes the one
#              bench one of these steps' oracles touches.
#
# Both scopes are statements about new code, so a tree that changed and added
# nothing passes vacuously *and says so out loud*: a gate that never examined
# anything reads exactly like a clean pass.
ENVELOPE=/tests/ENVELOPE.step${STEP}
[ -f "$ENVELOPE" ] || fail "no ENVELOPE.step<NN> file in /tests for step ${STEP}"
ENV_FILES=$(sed -e 's/#.*//' -e 's/[[:space:]]*$//' "$ENVELOPE" | grep -v '^$')
ENV_TOTAL=$(printf '%s\n' "$ENV_FILES" | grep -c .)
[ "${ENV_TOTAL:-0}" -ge 1 ] || fail "ENVELOPE.step${STEP} names no files"

# "Changed" is measured against /base, the ladder's own base tree, which is the
# same L0 build the denominator comes from: unchanged here means exactly "left
# as the tree it started from". A file absent from BOTH trees is not a change --
# simd_compress/x86.rs and neon.rs do not exist in the base, and a candidate who
# does not add them back has changed nothing. Absent on one side only is.
ENV_CHANGED=""
ENV_N=0
for f in $ENV_FILES; do
    if [ -e "/base/$f" ] && [ -e "/app/vortex/$f" ]; then
        if ! cmp -s "/base/$f" "/app/vortex/$f"; then
            ENV_CHANGED="$ENV_CHANGED $f"
            ENV_N=$((ENV_N + 1))
        fi
    elif [ -e "/base/$f" ] || [ -e "/app/vortex/$f" ]; then
        ENV_CHANGED="$ENV_CHANGED $f"
        ENV_N=$((ENV_N + 1))
    fi
done

# The added set is derived from the two trees rather than listed, so it cannot
# drift from what the candidate actually wrote. It is scoped to `src/` trees:
# that is what is compiled into the measured library, and it leaves a scratch
# benchmark -- which BENCHMARK.md invites -- out of the envelope's way.
if [ -d /app/vortex ]; then
    ENV_NEW=$(cd /app/vortex && find . -type f -name '*.rs' -path '*/src/*' -printf '%P\n' | sort)
    ENV_NEW_N=0
    for f in $ENV_NEW; do
        # ENV_FILES is multi-line, so membership is a whole-line match rather
        # than a `case` on a space-padded string -- a `case "* $f *"` misses
        # every line but the first and re-counts this step's own added files as
        # the candidate's. Measured on the oracle tree: 6 of 6 instead of 4 of 4.
        printf '%s\n' "$ENV_FILES" | grep -Fxq "$f" && continue
        [ -e "/base/$f" ] && continue
        ENV_CHANGED="$ENV_CHANGED $f"
        ENV_NEW_N=$((ENV_NEW_N + 1))
    done
    ENV_TOTAL=$((ENV_TOTAL + ENV_NEW_N))
    ENV_N=$((ENV_N + ENV_NEW_N))
    [ "$ENV_NEW_N" -eq 0 ] \
        || log "envelope: ${ENV_NEW_N} added source file(s) outside this step's list -- checking"
else
    fail "no /app/vortex tree to scan"
fi

if [ "$ENV_N" -eq 0 ]; then
    log "envelope: 0 of ${ENV_TOTAL} candidate files changed or added -- check skipped (an unchanged tree keeps the behaviour it started with, and the envelope is a statement about new code)"
else
    log "envelope: ${ENV_N} of ${ENV_TOTAL} candidate files changed or added -- checking"
    ENV_BAD=""
    for f in $ENV_CHANGED; do
        # A file the candidate deleted removes code; it cannot add a capability.
        # Whether the deletion breaks behaviour is a different question, and the
        # digest and suite gates below are the ones that ask it.
        [ -e "/app/vortex/$f" ] || continue
        # Comments, string literals and `#[cfg(test)]` items are stripped before
        # matching: a comment that names a register width or a thread is not an
        # implementation of one, and neither is a mention in a string or a test
        # module, which no build the verifier makes compiles. (A gate that
        # matched a comment did fail an oracle on one inside assembly, which is
        # why this is a scan of the code and not of the bytes.)
        [ -r "/app/vortex/$f" ] || fail "envelope: /app/vortex/$f is not readable"
        ENV_CODE=$(python3 /tests/vendor/scan_code.py "/app/vortex/$f") \
            || fail "envelope: could not scan /app/vortex/$f"
        # 1. ISA ceiling / register width. The envelope has no AVX-512: an
        #    AVX-512 intrinsic or feature in a candidate's file is only inside
        #    it if the same file probes for the feature at run time, which is
        #    the crate's own CpuKernel idiom. Without the probe the code
        #    requires a capability the grading host does not have.
        if printf '%s\n' "$ENV_CODE" | grep -qiE '_mm512_|avx512'; then
            if ! printf '%s\n' "$ENV_CODE" | grep -q 'is_x86_feature_detected'; then
                ENV_BAD="$ENV_BAD $f(avx512-without-runtime-probe)"
                continue
            fi
        fi
        # 2. Threads. The scored metric is wall-clock time for one call, so a
        #    kernel that spawns threads measures the machine, not the kernel.
        if printf '%s\n' "$ENV_CODE" | grep -qE 'std::thread|thread::spawn|thread::scope|rayon|par_iter|ThreadPool'; then
            ENV_BAD="$ENV_BAD $f(spawns-threads)"
        fi
    done
    if [ -n "$ENV_BAD" ]; then
        log "envelope violation(s):$ENV_BAD"
        fail "capability envelope violated:$ENV_BAD"
    fi
    log "envelope: all ${ENV_N} changed or added file(s) inside the envelope"
fi

# ------------------------------------------------------------- base measurement
# /opt/baseprobe was built in this image against /base. It is root-owned and
# read-only to the unprivileged user, and it runs BEFORE any candidate code
# exists in this container, so the denominator cannot be influenced.
taskset -c 0 /opt/baseprobe --step "$STEP" --reps "$REPS" --seed "$SEED" \
    > /opt/base.json 2>/opt/base.err \
    || fail "base probe failed to run"
test -s /opt/base.json || fail "base probe produced no output"

# --------------------------------------------------------- candidate build
give_app_to_unprivileged
rm -rf /work/cand
mkdir -p /work/cand/src
cp /tests/vendor/harness/probe.rs /work/cand/src/main.rs
sed -e "s#@TREEROOT@#/app/vortex#g" -e 's/^name = "probe"$/name = "probe-cand"/' \
    /tests/vendor/harness/Cargo.toml.in > /work/cand/Cargo.toml
# Pin the resolution. The probe crate is its own workspace root, so cargo
# resolves its dependency versions from the registry index rather than from the
# tree's Cargo.lock -- and offline, a version that resolves but was never
# downloaded fails the build with a "would make an HTTP request" error that
# reads like a broken candidate. The lock baked beside the base probe is the
# same resolution against the same manifests, and it is portable between trees
# (verified: the lock is byte-identical for two different tree paths and
# contains no absolute paths).
if [ -f /opt/probe.Cargo.lock ]; then
    cp /opt/probe.Cargo.lock /work/cand/Cargo.lock
fi
chown -R "$SCORE_UID:$SCORE_GID" /work/cand

BUILD_LOG=/work/cand-build.log
BUILD_FAILED=0
run_unprivileged env CARGO_TARGET_DIR=/work/tgt \
    /usr/local/cargo/bin/cargo build --release --offline --manifest-path /work/cand/Cargo.toml \
    > "$BUILD_LOG" 2>&1 || BUILD_FAILED=1

# ------------------------------------------------- root PATH, now system-only
# Everything below this line runs as root and must not resolve a command through
# a directory the candidate can write.
#
# give_app_to_unprivileged above makes /usr/local/cargo a+rwX so the offline
# build can open cargo's package-cache lock, and /usr/local/cargo/bin is first on
# PATH. The candidate's own build scripts run during that build -- as nobody, but
# they are candidate code and they can write into that directory. A planted
# `taskset`, `python3` or `setpriv` would then be executed by ROOT by the probes,
# the scorer or the suite below, which would hand the candidate the denominator
# and the reward file. That is exactly what harden_reward_dir exists to prevent,
# and it cannot prevent it while candidate-authored code runs as root.
#
# The build script runs *inside* that one command, so the reset belongs
# immediately after it, on both the success and the failure path. The failure
# path below logs through `tail`, and a build script that plants
# /usr/local/cargo/bin/tail and then fails the build on purpose would otherwise
# have it run as root -- the same hijack, one command earlier than the probes.
#
# So: reset PATH to the system directories, which nothing the candidate controls
# is ever writable, and invoke cargo by absolute path (a rustup shim; verified
# runnable by nobody). The build above is unaffected, and the candidate code that
# does run from here on -- the probe -- is dropped to nobody as well.
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH

if [ "$BUILD_FAILED" -ne 0 ]; then
    log "candidate build failed; tail follows"
    tail -n 40 "$BUILD_LOG" >&2
    fail "candidate_build_failed"
fi
CAND=/work/tgt/release/probe-cand
[ -x "$CAND" ] || fail "candidate probe binary missing after a successful build"

# ------------------------------------------------- base re-measurement, paired
# The gate below compares the candidate's end-to-end scan against the base's, and
# without this they are timed ~25 minutes apart: the first base probe runs in an
# idle container, the candidate probe after a long build. On a shared host the
# candidate probe is then scheduled against whatever else is running and can read
# far slower -- which is exactly what happened to this package's own oracle run,
# scoring it 0 for a regression that does not exist (1.3x-2.0x faster on every
# scored row, 0 new suite failures, digests matching).
#
# Re-measuring the base here, back to back with the candidate and in the same
# machine state, makes the comparison paired. The first probe is kept as the
# fallback and both are logged, so drift between them is visible rather than
# hidden.
#
# This does not weaken the denominator. /opt/baseprobe is a root-owned binary
# baked into the image before any candidate code existed in the container, so
# neither base run can be influenced by the candidate; the second one only
# changes WHEN the denominator is sampled, not who samples it.
BASE_JSON=/opt/base.json
if /usr/bin/taskset -c 0 /opt/baseprobe --step "$STEP" --reps "$REPS" --seed "$SEED" \
        > /opt/base2.json 2>/opt/base2.err && [ -s /opt/base2.json ]; then
    BASE_JSON=/opt/base2.json
else
    log "second base probe did not run; falling back to the first (unpaired)"
fi

# --------------------------------------------------------- candidate measurement
# The candidate's crate is linked into this binary, so its code runs here.
# Dropped to nobody: it is untrusted code inside the container that holds the
# reward directory and the denominator.
run_unprivileged /usr/bin/taskset -c 0 "$CAND" --step "$STEP" --reps "$REPS" --seed "$SEED" \
    > /work/cand.json 2>/work/cand.err \
    || { tail -n 40 /work/cand.err >&2; fail "candidate probe failed to run"; }
test -s /work/cand.json || fail "candidate probe produced no output"

# ------------------------------------------------------------------- scoring
# One speedup per scored function, plus a bit-exactness check on each and an
# end-to-end non-regression gate whose baseline is the L0 floor.
OUT=$(awk -v scope="$SCOPE" -v basef="$BASE_JSON" -v candf=/work/cand.json '
    function jget(line, key,   m, pat) {
        pat = "\"" key "\":[^,}]*"
        if (match(line, pat)) {
            m = substr(line, RSTART + length(key) + 3, RLENGTH - length(key) - 3)
            gsub(/"/, "", m)
            return m
        }
        return ""
    }
    BEGIN {
        while ((getline l < basef) > 0) {
            k = jget(l, "func")
            if (k != "") { bns[k] = jget(l, "ns"); bd[k] = jget(l, "digest") }
        }
        close(basef)
        while ((getline l < candf) > 0) {
            k = jget(l, "func")
            if (k != "") { cns[k] = jget(l, "ns"); cd[k] = jget(l, "digest") }
        }
        close(candf)

        n = 0; correct = 1
        while ((getline row < scope) > 0) {
            if (row ~ /^[[:space:]]*#/ || row ~ /^[[:space:]]*$/) continue
            split(row, f, /[[:space:]]+/)
            k = f[1]
            if (!(k in bns) || !(k in cns)) { missing = missing " " k; continue }
            if (bd[k] != cd[k]) { correct = 0; wrong = wrong " " k }
            v = cns[k] + 0.0
            if (v <= 0) { badtime = badtime " " k; continue }
            sp = (bns[k] + 0.0) / v
            if (sp > 1000) sp = 1000          # cap: no kernel is 1000x
            if (sp < 0.001) sp = 0.001
            n++; speedups[n] = sp
        }
        close(scope)

        if (missing != "") { print "MISSING" missing; exit 0 }
        if (badtime != "") { print "BADTIME" badtime; exit 0 }
        if (n == 0) { print "EMPTY"; exit 0 }

        printf "CORRECT %d\n", correct
        for (i = 1; i <= n; i++) printf "%.6f\n", speedups[i]
    }
' /dev/null)

case "$OUT" in
    MISSING*) fail "probe did not report rows:$OUT" ;;
    BADTIME*) fail "non-positive timing for:$OUT" ;;
    EMPTY)    fail "no scored row was measured" ;;
esac

CORRECT=$(printf '%s\n' "$OUT" | head -n1 | awk '{print $2}')
SPEEDUPS=$(printf '%s\n' "$OUT" | tail -n +2 | tr '\n' ' ')
[ -n "$SPEEDUPS" ] || fail "no speedups computed"

# --------------------------------------------- project suite, relative to base
# A stripped tree fails some of upstream's own tests by itself, so the gate is
# "no NEW failures": the base's list was recorded when this image was built.
SUITE_LOG=/work/suite.log
run_unprivileged env CARGO_TARGET_DIR=/work/tgt \
    /usr/local/cargo/bin/cargo test --release --offline --manifest-path /app/vortex/Cargo.toml \
    -p vortex-buffer -p vortex-mask -p vortex-array --lib \
    > "$SUITE_LOG" 2>&1 || true

awk '/^test .* FAILED$/ { sub(/^test /, ""); sub(/ \.\.\. FAILED$/, ""); print }' \
    "$SUITE_LOG" | sort -u > /work/cand_failures.txt
RAN=$(grep -c '^test ' "$SUITE_LOG" || true)
if [ "${RAN:-0}" -lt 100 ]; then
    log "project suite ran only ${RAN} tests; tail follows"
    tail -n 40 "$SUITE_LOG" >&2
    fail "project_suite_did_not_run"
fi
sort -u /opt/base_failures.txt > /work/base_failures.txt
comm -23 /work/cand_failures.txt /work/base_failures.txt > /work/new_failures.txt
NEW=$(wc -l < /work/new_failures.txt)
log "project suite: ${RAN} test lines, $(wc -l < /work/cand_failures.txt) failing, ${NEW} new"

# ------------------------------------------------------ end-to-end regression
# Non-regression, not scored: the per-function harness must not be won at the
# real pipeline's expense. Baseline is L0 (this task's base), never L(k-1).
#
# The bound is 3x, not 1.5x, and that is a measured number rather than a
# generous one. This row is memory-bandwidth-bound and it is the noisiest thing
# the probe measures. Three same-workload measurements taken minutes apart on a
# host running other jobs spread over 748k-887k ns (1.19x) for trees whose source
# is IDENTICAL on both sides, and an oracle pair has been observed at 1.94x when
# two verifiers happened to be timing their pinned probes on the same core --
# `taskset -c 0` restricts the probe to a core, it does not give it the core. At
# 1.5x that noise zeroed the oracle twice; at 3x it cannot, while a genuine
# pipeline regression (which is a 5-50x effect, not a 2x one) still fires it.
#
# This gate is a backstop, and it is deliberately NOT part of `correctness`: a
# tree that is slower is not a tree that behaves differently, so the gate zeroes
# the step's reward and leaves the correctness field saying what it was asked to
# say. Bit-exactness on every scored row and the project suite are what decide
# `correctness`; this only stops a candidate from buying the per-function rows
# by wrecking the pipeline they are part of.
E2E=$(awk -v candf=/work/cand.json -v basef="$BASE_JSON" '
    function jget(line, key,   m, pat) {
        pat = "\"" key "\":[^,}]*"
        if (match(line, pat)) {
            m = substr(line, RSTART + length(key) + 3, RLENGTH - length(key) - 3)
            gsub(/"/, "", m); return m
        }
        return ""
    }
    BEGIN {
        while ((getline l < basef) > 0)
            if (jget(l, "func") == "__e2e") {
                bns = jget(l, "ns"); bdg = jget(l, "digest")
            }
        close(basef)
        # The FIRST base sample, read only so its drift from the paired one is
        # visible in the log. Never used for scoring or for the gate.
        while ((getline l < "/opt/base.json") > 0)
            if (jget(l, "func") == "__e2e") b1ns = jget(l, "ns")
        close("/opt/base.json")
        while ((getline l < candf) > 0)
            if (jget(l, "func") == "__e2e") {
                ns = jget(l, "ns"); dg = jget(l, "digest")
            }
        close(candf)
        printf "%s %s %s %s %s\n", bns, ns, bdg, dg, b1ns
    }
' /dev/null)
set -- $E2E
BASE_E2E_NS=${1:-}; CAND_E2E_NS=${2:-}; BASE_E2E_D=${3:-}; CAND_E2E_D=${4:-}
BASE1_E2E_NS=${5:-}
# A gate that never examined anything reads exactly like a clean pass, so the
# missing-row case is a loud failure rather than a silently skipped check.
if [ -z "$BASE_E2E_D" ] || [ -z "$CAND_E2E_D" ]; then
    fail "probe produced no __e2e row"
fi
[ "$BASE_E2E_D" = "$CAND_E2E_D" ] || CORRECT=0
E2E_OK=1
awk -v b="$BASE_E2E_NS" -v c="$CAND_E2E_NS" 'BEGIN { exit !(b > 0 && c > 0 && c <= b * 3.0) }' \
    || E2E_OK=0
log "end-to-end: base ${BASE_E2E_NS}ns (first base sample ${BASE1_E2E_NS:-absent}ns) cand ${CAND_E2E_NS}ns digest $([ "$BASE_E2E_D" = "$CAND_E2E_D" ] && echo match || echo MISMATCH)"

# ------------------------------------------------------------------- verdict
# `correctness` is one statement: this tree behaves exactly like the tree it
# started from. Everything in this block is a behaviour question -- a differing
# digest, a test that newly fails, an end-to-end result that differs -- and
# every one of them is 1 for a tree that changed nothing, by definition.
if [ "$NEW" -ne 0 ]; then
    log "new project-suite failures:"; cat /work/new_failures.txt >&2
    CORRECT=0
fi
# The end-to-end *timing* bound below is not a behaviour question: "slower" is
# not "different". It zeroes the step's reward, which is where a regression
# belongs, and leaves `correctness` to say what it was asked to say. The score
# it produces is identical to the one a correctness failure would -- the
# difference is what the two fields mean, and a no-op that loses the row to
# noise on a shared host must not be reported as having changed behaviour.
if [ "$E2E_OK" -ne 1 ]; then
    log "end-to-end non-regression gate failed (candidate slower than 3x the L0 floor, which no measurement of a correct tree has come near) -- reward zeroed, correctness unaffected"
    E2E_GATE=0
fi

# ------------------------------------------- non-scored similarity diagnostic
# Of the lines the candidate added to this step's own files, what fraction
# appears verbatim in upstream's version of the same file. Validated to read
# ~1.0 for the oracle and 0.0 for the no-op. It is a lower bound, not a gate.
SIMILARITY=$(python3 /tests/similarity.py /opt/upstream /base /app/vortex 2>/dev/null || echo 0)
export SIMILARITY
log "similarity diagnostic: ${SIMILARITY}"

report "$CORRECT" $SPEEDUPS
