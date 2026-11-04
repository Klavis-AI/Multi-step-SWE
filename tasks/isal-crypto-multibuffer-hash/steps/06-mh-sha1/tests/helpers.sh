#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. This file is
# byte-identical in every step -- a substitution is a no-op -- and holds no
# per-step knowledge.
set -u

# Unprivileged identity for candidate-derived code. nobody:nogroup already
# exists in debian:bookworm-slim.
SCORE_UID=65534
SCORE_GID=65534

# Lock the reward directory to root, before anything else runs.
#
# Harbor chmods /logs/verifier to 777 just before this container starts, so
# anything running here can overwrite reward.json -- including candidate code,
# which this verifier compiles and executes. Writing the real score last does
# not help: a detached child outlives the scorer.
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 700 /logs/verifier
}

# Run candidate-derived code as nobody. Needed for harden_reward_dir to mean
# anything: root can write a root-owned directory whatever its mode. Inside the
# verifier this container runs as root and the de-escalation is real; outside it
# (an author's dry run) it degrades to a plain exec rather than failing.
run_unprivileged() {
    if [ "$(id -u)" -eq 0 ] && command -v setpriv > /dev/null 2>&1; then
        setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
            env HOME=/tmp TMPDIR=/tmp "$@"
    else
        env HOME=/tmp TMPDIR=/tmp "$@"
    fi
}

# Hand a candidate-controlled tree to the unprivileged identity.  The build
# recipes in it (Makefile.unx, every unit's Makefile.am) are files the
# candidate wrote, and `make` runs them -- as root, which is exactly what
# harden_reward_dir cannot survive: root writes a root-owned 0700 directory
# whatever its mode.  Outside the verifier (an author's dry run, no root) this
# degrades to a no-op, like run_unprivileged.
give_app_to_unprivileged() {
    [ "$(id -u)" -eq 0 ] || return 0
    chown -R "$SCORE_UID:$SCORE_GID" "$1" 2>/dev/null || true
}

build_tree() {
    # $1 = tree. Clean first: the candidate's own objects must not be able to
    # stand in for a build of the source it actually left behind.
    ( cd "$1" && rm -rf bin && find . -maxdepth 1 -name '*.o' -delete ) 2>/dev/null
    ( cd "$1" && run_unprivileged make -f Makefile.unx lib -j"$(nproc)" ) > /tmp/build.log 2>&1
}

build_harness() {
    # $1 = tree, $2 = output binary
    local inc="" d
    for d in $STAGE_DIRS; do inc="$inc -I $1/$d"; done
    gcc -O2 -o "$2" "${TESTS_DIR:-/tests}/harness/bench.step$STEP.c" \
        -I "${TESTS_DIR:-/tests}/harness" \
        -I "$1" -I "$1/include" -I "$1/include/isa-l_crypto" -I "$1/include/internal" \
        $inc "$1/bin/isa-l_crypto.a" > /tmp/harness.log 2>&1
}

# Names of the project's own check tests that fail in tree $1. make -k so one
# failure does not hide the rest; the failing target is named in make's own
# error line, which is the stable thing to parse.
#
# The run's output is read through a pipe this shell owns and only then written
# to the log.  What make runs here is the candidate's own test binaries, as the
# unprivileged identity, and they share the pipeline's stdout; a child that
# seeks back on a log file this shell opened can rewrite evidence the verifier
# has already read -- the failure markers and the test count that decide two of
# this step's gates.  Reading through a pipe first is the same cure the digest
# streams get in test.sh: the bytes that land in the log are the bytes the
# suite printed.
project_failures() {
    ( cd "$1" && run_unprivileged make -f Makefile.unx checks -j"$(nproc)" ) > /tmp/checks.log 2>&1 || true
    suite_out=$( cd "$1" && run_unprivileged make -k -f Makefile.unx check 2>&1 ) || true
    printf '%s\n' "$suite_out" > /tmp/checkrun.log
    grep -oE '\[[^]]*: ([^] ]+)\.run\]' /tmp/checkrun.log \
        | sed -E 's/.*: (.*)\.run\]/\1/' | sort -u
}

# verbatim_line_fraction <added-lines-file> <upstream-lines-file>
#
# Non-scored similarity diagnostic. Of the non-blank source lines the candidate
# added relative to this step's base, the fraction that occurs verbatim in
# upstream's implementation of the same directories.
#
# It is a VERBATIM-LINE measure, so cosmetically rewritten code reads near zero:
# it is a lower bound on copying and never a clean bill of health. It is not a
# gate -- the oracle IS upstream, so any threshold would fail the reference
# solution. Reported, not scored.
verbatim_line_fraction() {
    local added="$1" upstream="$2" total matched
    total=$(wc -l < "$added" | tr -d ' ')
    if [ "${total:-0}" -eq 0 ]; then
        printf '0.0000 0 0'
        return 0
    fi
    matched=$(comm -12 <(sort -u "$added") "$upstream" | wc -l | tr -d ' ')
    awk -v m="$matched" -v t="$total" 'BEGIN { printf "%.4f %d %d", m / t, m, t }'
}

# report <correctness 0|1> <speedup> [<speedup> ...]
#
#   reward  = 0                                   if correctness != 1 or geomean <= 1
#           = 1                                   if geomean >= ORACLE_BASELINE
#           = ln(geomean) / ln(ORACLE_BASELINE)      otherwise, clamped to [0, 1]
#   geomean = exp( mean_f( ln(max(1, speedup_f)) ) )
#
# One speedup per scored unit. The geometric mean is the right aggregator for
# ratios, and it penalises neglect: a unit left alone contributes ln(1) = 0
# and drags the step down. max(1, .) floors a regression at 0 rather than going
# negative. Correctness is binary and multiplies.
#
# `geomean_speedup` is the geomean of the score's own unit ratios, NOT of the
# reward: deriving it as exp(reward) folds the correctness factor in, so a step
# whose units measured (say) 0.60x and 3.94x reported `geomean_speedup: 1.0000`
# once correctness zeroed the reward, and the trial-level mean then averaged
# that fabricated 1.0 into its own aggregate. With correctness 1 the two
# definitions agree exactly, so only a step that already scored 0 reads
# differently -- and it now reads what was actually measured.
#
# The reward is normalized by this step's hardcoded oracle baseline and clamped
# to [0, 1].
ORACLE_BASELINE=5.0221000000
report() {
    local correct="${1:-0}"; shift || true
    local reward geo pair
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
    local sim="0.0000 0 0"
    if [ -s /tmp/added_lines.txt ] && [ -s /opt/upstream_lines.txt ]; then
        sim=$(verbatim_line_fraction /tmp/added_lines.txt /opt/upstream_lines.txt)
    fi
    mkdir -p "${REWARD_DIR:-/logs/verifier}"
        # oracle binary step reward: 1 only above the hardcoded baseline.
    if [[ "${correct:-0}" != "1" ]] || ! awk -v g="${geo:-0}" -v b="$ORACLE_BASELINE" 'BEGIN { exit !(g > b) }'; then
        reward="0.000000"
    else
        reward="1.000000"
    fi
printf '{"reward":%s,"correctness":%s,"geomean_speedup":%s,"verbatim_line_fraction":%s}\n' \
        "$reward" "${correct:-0}" "$geo" "${sim%% *}" \
        > "${REWARD_DIR:-/logs/verifier}/reward.json"
    echo "REWARD correctness=${correct} speedups=[$*] geomean=${geo} reward=${reward}"
    sim_rest="${sim#* }"
    echo "  similarity: verbatim_line_fraction=${sim%% *} (matched=${sim_rest%% *} of ${sim_rest##* } added line(s); non-scored, verbatim-line lower bound on copying)"
    chmod 755 "${REWARD_DIR:-/logs/verifier}" 2>/dev/null || true
    chmod 644 "${REWARD_DIR:-/logs/verifier}/reward.json" 2>/dev/null || true
    exit 0
}
