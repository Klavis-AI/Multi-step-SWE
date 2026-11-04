#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Keep in sync.
set -u

# Unprivileged identity for candidate code. nobody:nogroup exists in
# debian:bookworm-slim.
SCORE_UID=65534
SCORE_GID=65534

# Lock the reward directory to root. Call this first in test.sh.
#
# Harbor chmods /logs/verifier to 777 just before this container starts, so
# anything running here can overwrite reward.json -- including candidate code
# the verifier builds or runs. Writing the real score last does not help: a
# detached child outlives the scorer and writes after it.
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 700 /logs/verifier
}

# Run candidate-authored code as nobody instead of root.
run_unprivileged() {
    setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        env HOME=/tmp TMPDIR=/tmp "$@"
}

# Give /app to the unprivileged user. Call before an unprivileged build.
give_app_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" /app 2>/dev/null || true
}

# The CPU to pin measurements to. A shared grading host is noisy; pinned
# single-core runs are the only way the ratio means anything. Read the
# container's allowed set rather than assuming CPU 0 exists.
pick_cpu() {
    local list first
    list=$(awk '/^Cpus_allowed_list/ {print $2}' /proc/self/status 2>/dev/null)
    [ -n "$list" ] || list=0
    first=${list%%,*}
    first=${first%%-*}
    taskset -c "$first" true 2>/dev/null || first=0
    printf '%s' "$first"
}

# report <correctness 0|1> <speedup> [<speedup> ...]
#
#   reward = 0                                   if correctness != 1 or geomean <= 1
#          = 1                                   if geomean >= ORACLE_BASELINE
#          = ln(geomean) / ln(ORACLE_BASELINE)      otherwise, clamped to [0, 1]
#   geomean = exp( mean_f( ln(max(1, speedup_f)) ))
#
# One speedup per (scoped entry point, parameter set) pair in this step's
# scope. The geometric mean is the right aggregator for ratios and it penalises
# neglect: a pair left alone contributes ln(1) = 0 and drags the step down, so
# a large win on one entry cannot hide doing nothing on another.
#
# max(1, .) floors a regression at 0 rather than going negative. Correctness is
# binary and multiplies, so a tree that changes behaviour scores 0 however fast.
# The reward is normalized by this step's hardcoded oracle baseline and clamped
# to [0, 1]. Harbor's multi_step_reward_strategy = "mean" averages it across
# steps.
ORACLE_BASELINE=5.0915000000
report() {
    local correct="${1:-0}"; shift || true
    local pair reward geo
    pair=$(awk -v c="$correct" -v b="$ORACLE_BASELINE" 'BEGIN {
        c += 0; if (c != 1) c = 0
        n = ARGC - 1; if (n <= 0) { printf "%.6f %.4f", 0, 1; exit }
        s = 0
        for (i = 1; i <= n; i++) { v = ARGV[i] + 0; if (v < 1) v = 1; s += log(v) }
        g = sprintf("%.4f", exp(s / n)) + 0
        if (c != 1 || g <= 1) r = 0
        else if (b <= 1 || g >= b) r = 1
        else { r = log(g) / log(b); if (r < 0) r = 0; if (r > 1) r = 1 }
        printf "%.6f %.4f", r, g
    }' "$@")
    reward=${pair%% *}
    geo=${pair##* }
    # Non-scored similarity diagnostic (see the task README): the fraction of
    # the lines this step added that appear verbatim in upstream's own copy of
    # the same file. Reported, never gated -- the reference solution IS
    # upstream, so any gate on it would fail the oracle.
    # reward.json must hold numbers only: Harbor parses every key under
    # `rewards` as a float, and a single string field aborts the trial.
    local sim="0.0"
    if [ -s /tmp/similarity.txt ]; then
        sim=$(cat /tmp/similarity.txt)
        case "$sim" in ''|*[!0-9.eE+-]*) sim="0.0" ;; esac
    fi
    mkdir -p /logs/verifier
        # oracle binary step reward: 1 only above the hardcoded baseline.
    if [[ "${correct:-0}" != "1" ]] || ! awk -v g="${geo:-0}" -v b="$ORACLE_BASELINE" 'BEGIN { exit !(g > b) }'; then
        reward="0.000000"
    else
        reward="1.000000"
    fi
printf '{"reward":%s,"correctness":%s,"geomean_speedup":%s,"similarity_upstream":%s}\n' \
        "$reward" "${correct:-0}" "$geo" "$sim" > /logs/verifier/reward.json
    echo "REWARD correctness=${correct} pairs=[$*] geomean=${geo} reward=${reward}"
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    exit 0
}

# require_step <NN>: fail loudly when this image's /tests came from another
# step's build cache. /etc/harbor-step-id names the image (written by the
# Dockerfile before `COPY . /tests`); the argument names the tests/ tree.
require_step() {
    local want="$1" got msg
    got=$(cat /etc/harbor-step-id 2>/dev/null) || got=MISSING
    [ "$got" = "$want" ] && return 0
    msg="VERIFIER/STEP MISMATCH: image is step ${got}, /tests tree is step ${want}."
    printf '%s\n' "$msg" >&2
    mkdir -p /logs/verifier 2>/dev/null || true
    printf '%s\n' "$msg" > /logs/verifier/step-mismatch.log
    # reward.json holds numbers only: Harbor parses every key as a float, and a
    # single string field aborts the trial (see report()).  The step ids are in
    # step-mismatch.log and on stderr; `step_mismatch` marks the case.
    printf '{"reward":0,"correctness":0,"step_mismatch":1}\n' > /logs/verifier/reward.json
    exit 0
}

# bind_digest: the image bakes the digest of every per-step data file next to
# the file itself, so a substituted data file fails at build time rather than
# grading the wrong scope. Re-checked here too, and it counts what it examined.
bind_digest() {
    local n
    [ -f /tests/DIGESTS ] || { echo "no /tests/DIGESTS"; return 1; }
    ( cd /tests && md5sum -c --quiet DIGESTS ) || return 1
    n=$(grep -c . /tests/DIGESTS)
    echo "  bound: ${n} per-step data file(s) match their baked digests"
    return 0
}

# build_tree <tree> <builddir>: build the static library and the project's own
# test binaries for all three parameter sets. Unused by test.sh, which builds
# the candidate's tree inline as nobody; kept consistent with that, since a
# tree's Makefile is a candidate-authored program and must never run as root.
build_tree() {
    local tree="$1" bld="$2" jobs="${3:-4}"
    run_unprivileged make -C "$tree" lib OPT=1 AUTO=1 BUILD_DIR="$bld" -j"$jobs" \
        > /tmp/build_lib.log 2>&1 || return 1
    run_unprivileged make -C "$tree" func_44 func_65 func_87 unit_44 unit_65 unit_87 kat_44 kat_65 kat_87 \
        OPT=1 AUTO=1 BUILD_DIR="$bld" -j"$jobs" > /tmp/build_tests.log 2>&1 || return 1
    return 0
}

# build_harness <tree> <builddir> <ps> <out> <hal_tree>
#
# Compiled against that tree's own headers and static library, so each binary
# times the implementation that tree installs. Flags are the project's own
# (test/mk/components.mk with OPT=1 AUTO=1), minus the warning-as-error
# switches, which are not the candidate's obligation to satisfy.
#
# <hal_tree> supplies the cycle counter and is this step's reference tree, never
# the candidate's: test/hal/hal.{c,h} is the measuring instrument, not part of
# the work, and a ratio whose numerator was timed with a counter the candidate
# can rewrite is not a measurement. It goes first on the include path so nothing
# the candidate added can shadow it. The reference harness is baked the same way
# (vendor/prepare_base.sh), so both sides read the same clock.
#
# Runs as nobody: everything it reads -- the candidate's headers and the
# candidate's static library -- is candidate-tree material.
build_harness() {
    local tree="$1" bld="$2" ps="$3" out="$4" hal="$5"
    run_unprivileged gcc -O2 -std=c99 -fomit-frame-pointer -mavx2 -mbmi2 \
        -DPMU_CYCLES -DMLD_FORCE_X86_64 \
        -DMLD_CONFIG_USE_NATIVE_BACKEND_ARITH \
        -DMLD_CONFIG_USE_NATIVE_BACKEND_FIPS202 \
        -DMLD_CONFIG_PARAMETER_SET="$ps" \
        -I"$hal/test/hal" -I"$tree" -I"$tree/mldsa" \
        -o "$out" /tests/vendor/bench_funcs.c "$bld/libmldsa$ps.a" "$bld/hal$ps.o" \
        > /tmp/harness.log 2>&1
    # Take the binary back to root, the way harden_reward_dir takes back the
    # reward directory: every scored number comes out of this file, and a
    # still-running nobody process could otherwise rewrite it to print whatever
    # it liked. 0755 still executes fine through run_unprivileged.
    chown root:root "$out" 2>/dev/null || true
}

# build_hal <hal_tree> <builddir> <ps>: the cycle-counter object for one
# parameter set, compiled from that tree's own test/hal/hal.c. The callers in
# test.sh pass this step's reference tree for the candidate's harness, so the
# numerator is read by the same counter the denominator was -- see
# build_harness. Runs as nobody: the compile is of tree material either way.
build_hal() {
    local tree="$1" bld="$2" ps="$3"
    run_unprivileged gcc -c -O2 -DPMU_CYCLES -DMLD_FORCE_X86_64 -DMLD_CONFIG_PARAMETER_SET="$ps" \
        -I"$tree" -I"$tree/mldsa" -I"$tree/test/hal" \
        "$tree/test/hal/hal.c" -o "$bld/hal$ps.o" > /tmp/hal.log 2>&1
}

# e2e_cycles <tree> <builddir> <ps> <reps> <cpu> [unprivileged]: keypair/sign/
# verify cycles, pinned to one core, minimum over `reps` runs. The minimum is
# the statistic that survives one-sided contention noise -- contention can only
# make a run slower -- and this host is shared with other tenants.
#
# With a non-empty sixth argument the timed binary runs through
# run_unprivileged: the candidate's tree is candidate-authored code and must not
# execute as root. The verifier-owned trees (/l0, /base) are timed as root.
e2e_cycles() {
    local tree="$1" bld="$2" ps="$3" reps="$4" cpu="$5" unpriv="${6:-}" best=""
    local i out
    for i in $(seq 1 "$reps"); do
        if [ -n "$unpriv" ]; then
            out=$(run_unprivileged taskset -c "$cpu" "$bld/mldsa$ps/bin/bench_mldsa$ps" 2>/dev/null \
                  | awk '/cycles \(avg\)/ {printf "%s ", $5}')
        else
            out=$(taskset -c "$cpu" "$bld/mldsa$ps/bin/bench_mldsa$ps" 2>/dev/null \
                  | awk '/cycles \(avg\)/ {printf "%s ", $5}')
        fi
        [ -n "$out" ] || continue
        best=$(min_vec "$best" "$out")
    done
    printf '%s' "$best"
}

# min_vec "<a b c>" "<d e f>" -> elementwise minimum, empty string is bottom
min_vec() {
    awk -v a="$1" -v b="$2" 'BEGIN {
        if (a == "") { print b; exit }
        if (b == "") { print a; exit }
        n = split(a, x, " "); m = split(b, y, " ")
        for (i = 1; i <= n && i <= m; i++) printf "%s ", (x[i] < y[i]) ? x[i] : y[i]
    }'
}
