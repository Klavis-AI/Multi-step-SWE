#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Keep in sync.
set -u

# Unprivileged identity for candidate code. nobody:nogroup already exists in
# debian:bookworm-slim.
SCORE_UID=65534
SCORE_GID=65534

# Lock the reward directory to root. Call this first in test.sh.
#
# Harbor chmods /logs/verifier to 777 just before this container starts, so
# anything running here can overwrite reward.json -- including candidate code
# the verifier builds or runs. Writing the real score last does not help: a
# detached child outlives the scorer and writes after it.
#
# This runs after Harbor's chmod, so this is the mode that sticks.
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 700 /logs/verifier
}

# Run candidate-authored code as nobody instead of root.
#
# Needed for harden_reward_dir to mean anything: root can write a root-owned
# directory whatever its mode. Use it everywhere candidate code runs -- a
# Makefile, a linked library's static initializer, a probe compiled against
# candidate source -- not just a service.
run_unprivileged() {
    setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        env HOME=/tmp TMPDIR=/tmp "$@"
}

# Give /app to the unprivileged user. Call before an unprivileged build.
#
# /app arrives owned by whoever wrote the artifact. Without this the build
# cannot create its output directory, so every candidate scores 0 and the step
# looks merely very hard instead of broken.
give_app_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" /app 2>/dev/null || true
}

# report <correctness 0|1> <speedup> [<speedup> ...]
#
#   reward = 0                                   if correctness != 1 or geomean <= 1
#          = 1                                   if geomean >= ORACLE_BASELINE
#          = ln(geomean) / ln(ORACLE_BASELINE)      otherwise, clamped to [0, 1]
#   geomean = exp( mean_f( ln(max(1, speedup_f)) ))
#
# One speedup per scoped function, from a harness that calls the entry point
# the library actually installed, in this step's base build and in the
# candidate, on inputs drawn in the verifier from a verifier-owned seed. The
# geometric mean penalises neglect: a function left alone contributes
# ln(1) = 0, so a large win on one function cannot hide doing nothing on
# another. max(1, .) floors a regression at 0 rather than going negative.
# Correctness is binary and multiplies.
ORACLE_BASELINE=2.4464000000
report() {
    local correct="${1:-0}"; shift || true
    local nfun=$#
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
    mkdir -p /logs/verifier
        # oracle binary step reward: 1 only above the hardcoded baseline.
    if [[ "${correct:-0}" != "1" ]] || ! awk -v g="${geo:-0}" -v b="$ORACLE_BASELINE" 'BEGIN { exit !(g > b) }'; then
        reward="0.000000"
    else
        reward="1.000000"
    fi
printf '{"reward":%s,"correctness":%s,"geomean_speedup":%s,"functions_measured":%s}\n' \
        "$reward" "${correct:-0}" "$geo" "$nfun" > /logs/verifier/reward.json
    echo "REWARD correctness=${correct} speedups=[$*] geomean=${geo} reward=${reward}"
    # Reopen the directory to 0755 on the way out: a local docker run collects
    # this file from the host as an ordinary user, where a 0700 directory reads
    # as a *missing* reward. The directory and the file stay root-owned, and
    # 0755 leaves nobody no write bit on either, so the candidate still cannot
    # write or replace it.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    exit 0
}

# build_tree <srctree> <builddir>
#
# The tree and its Makefile are the candidate's: `make` executes the tree's own
# recipes, so the build runs as the unprivileged user. The build directory is
# created and handed to that user first, or the build cannot write its output
# and every candidate scores 0.
build_tree() {
    mkdir -p "$2" 2>/dev/null || true
    chown "$SCORE_UID:$SCORE_GID" "$2" 2>/dev/null || true
    ( cd "$1" && run_unprivileged make lib OPT=1 AUTO=1 -j4 BUILD_DIR="$2" ) > /tmp/build.log 2>&1 || {
        echo "  build failed"; tail -25 /tmp/build.log; return 1; }
}

# The project's own suites, one make target per (suite, parameter set). They are
# run individually so a failure is attributable, and judged *relative to the
# base*: a stripped tree can fail some of upstream's expectations by itself, and
# demanding zero failures would score a correct no-op at 0.
SUITE_TARGETS="run_func_512 run_func_768 run_func_1024 run_kat_512 run_kat_768 run_kat_1024"

# suite_failures <srctree> <builddir>  -> one failing target per line on stdout.
# Also records how many targets were attempted, so the caller can tell a clean
# run from a vacuous one.
#
# These targets both build the candidate's tree (its Makefile) and run the test
# binaries linked against it, so the whole target runs as the unprivileged user.
suite_failures() {
    local tree="$1" bld="$2" t n=0
    : > /tmp/suite_failures.txt
    for t in $SUITE_TARGETS; do
        n=$((n+1))
        if ! ( cd "$tree" && run_unprivileged make "$t" OPT=1 AUTO=1 -j4 BUILD_DIR="$bld" ) \
                > "/tmp/$t.log" 2>&1; then
            echo "$t" | tee -a /tmp/suite_failures.txt
        fi
    done
    echo "$n" > /tmp/suite_count.txt
}

# speedup <numerator cycles> <denominator cycles>
speedup() {
    awk -v a="${1:-0}" -v b="${2:-0}" 'BEGIN{ if (b+0<=0) print 0; else printf "%.4f", a/b }'
}

# median_speedup <base-harness> <cand-harness> <fnid> <seed> [rounds]
#
# The two harnesses are separate binaries, so their timings are not taken at the
# same instant and a contended host moves them independently: measured on this
# box, a single ratio for an unchanged tree ranged from 0.98x to 2.16x.
# Alternating the two measurements and taking the median of the per-round
# ratios makes the contention common-mode instead.
#
# Each round draws its own inputs: the seed advances with the round index, and
# both sides of a round always get the same one, so the comparison stays
# paired. Re-using one seed for all rounds would re-present the same inputs
# nine times, and a cache that survives the process (a file, an mmap) would
# then be warm from round two on -- measured on this package, that scored
# 1.45x on step 01 where the oracle scores 1.38x.
#
# Prints "<ratio> <base cycles> <candidate cycles> <rounds used>", or
# "none ? ? 0" when a side produced no number at all. The cycle pair printed
# is the pair the median ratio was taken from -- printing the last round's
# pair instead reports two numbers that need not be in the ratio at all.
#
# The candidate harness is built from the candidate's tree and runs as the
# unprivileged user; the base harness is built into the verifier image and
# stays root.
median_speedup() {
    local ha="$1" hc="$2" fn="$3" sd="$4" rounds="${5:-7}"
    local i tb tc m
    local -a r=()
    for ((i = 0; i < rounds; i++)); do
        tb=$(taskset -c 0,1 "$ha" "$fn" "$((sd + i))" bench 2>/dev/null | sed 's/cycles=//')
        tc=$(run_unprivileged taskset -c 0,1 "$hc" "$fn" "$((sd + i))" bench 2>/dev/null | sed 's/cycles=//')
        if [ -n "$tb" ] && [ -n "$tc" ]; then
            r+=("$(speedup "$tb" "$tc") $tb $tc")
        fi
    done
    if [ "${#r[@]}" -eq 0 ]; then echo "none ? ? 0"; return; fi
    m=$(printf '%s\n' "${r[@]}" | sort -n \
        | awk -v n="${#r[@]}" 'NR==int((n+1)/2){print}')
    echo "$m ${#r[@]}"
}

E2E_OPS="kem_keypair kem_encaps kem_decaps"

# e2e_regression <candidate harness> <neutralised harness> [rounds]
#
# Total cycles for key generation, encapsulation and decapsulation at
# ML-KEM-768 -- the ladder's parameter set -- for the tree as delivered against
# the same tree with this step's dispatch neutralised. The two are separate
# builds, so each round measures them back to back, one operation at a time, and
# the reported ratio is the median of the per-round ratios: contention that
# moves one side moves the other too.
#
# What this gate can and cannot resolve. Rebuilds of a tree differ in link
# order, and code layout alone moves a whole-program figure by several percent;
# on a contended host the measured spread of this ratio for trees whose
# pipelines are equivalent is roughly 0.85-1.15. The threshold is set at the top
# of that band, so the gate answers "did the pipeline get materially slower",
# not "did it move at all" -- it is a guard, never a score.
# Prints "<median ratio> <delivered cycles> <neutralised cycles>", the cycle
# pair being the one the median ratio was taken from. As in median_speedup,
# each round draws its own inputs (seed + round index), both sides sharing it.
#
# Both harnesses here are built from the candidate's tree (delivered, and the
# same tree with this step's dispatch stubbed out), so both run unprivileged.
e2e_regression() {
    local ha="$1" hb="$2" rounds="${3:-7}" i f va vb sa sb
    local -a r=()
    for ((i = 0; i < rounds; i++)); do
        sa=0; sb=0
        for f in $E2E_OPS; do
            va=$(run_unprivileged taskset -c 0,1 "$ha" "$f" "$((777 + i))" bench 2>/dev/null | sed 's/cycles=//')
            vb=$(run_unprivileged taskset -c 0,1 "$hb" "$f" "$((777 + i))" bench 2>/dev/null | sed 's/cycles=//')
            sa=$((sa + ${va:-0})); sb=$((sb + ${vb:-0}))
        done
        if [ "$sa" -gt 0 ] && [ "$sb" -gt 0 ]; then
            r+=("$(speedup "$sa" "$sb") $sa $sb")
        fi
    done
    if [ "${#r[@]}" -eq 0 ]; then echo "none 0 0"; return; fi
    printf '%s\n' "${r[@]}" | sort -n \
        | awk -v n="${#r[@]}" 'NR==int((n+1)/2){print}'
}
