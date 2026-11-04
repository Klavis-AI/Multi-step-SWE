#!/usr/bin/env bash
# Shared verifier helpers. The verifier image is built with this step's tests/
# as its Docker build context. The oracle baseline bound just above report() is
# the one per-step value; everything else is common to every step.
set -u

# The canonical configure flags, library targets and upstream pin live in one
# file, so the verifier and the author cannot drift apart.
. /tests/vendor/PIN

# ---------------------------------------------------------------------------
# The reward directory, and the identity candidate code runs as.
# ---------------------------------------------------------------------------

# Unprivileged identity for candidate code. nobody:nogroup already exists in
# debian:bookworm-slim.
SCORE_UID=65534
SCORE_GID=65534

# Lock the reward directory to root. Call this first in test.sh: it runs before
# any candidate code does.
#
# Harbor chmods /logs/verifier to 777 just before this container starts, so
# anything running here can overwrite reward.json -- including candidate code
# the verifier builds or runs: a CMakeLists the configure executes, a linked
# library's static initializer, the project's own test binaries. Writing the
# real score last does not help: a detached child outlives the scorer and writes
# after it.
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
# Makefile, a CMakeLists, a linked library's static initializer, a probe
# compiled against candidate source -- not just a service.
run_unprivileged() {
    setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        env HOME=/tmp TMPDIR=/tmp "$@"
}

# Give /app to the unprivileged user. Call before an unprivileged build.
#
# /app arrives owned by whoever wrote the artifact. Without this the build
# cannot create its output, so every candidate scores 0 and the step looks
# merely very hard instead of broken.
give_app_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" /app 2>/dev/null || true
}

# The command prefix every build of the CANDIDATE's tree goes through. test.sh
# sets it to run_unprivileged before the first candidate build, and back to
# empty around the verifier's own builds (the base tree, the reference harness):
# those are the denominator and are never part of what the candidate controls,
# so they keep running as root.
#
# A build is candidate code: `cmake` executes the tree's CMakeLists.txt, and the
# build it generates runs whatever rules and commands that tree defines.
RUN_CANDIDATE=""

# Create the directories an unprivileged build writes to and give them to the
# build user: the configure/build directories (out of source, so they have to
# exist and be writable), the scratch directory, and a ccache directory.
#
# The image's own cache (ENV CCACHE_DIR=/root/.ccache) sits inside root's home,
# which the unprivileged user cannot enter, and a ccache that cannot create its
# directory fails the compile it was asked to accelerate. The unprivileged build
# is therefore given a cache directory of its own. A build cache is a build-time
# optimisation and not a measured value: an empty one only makes the first
# compile slower.
prepare_unprivileged_dirs() {
    mkdir -p "$@"
    chown -R "$SCORE_UID:$SCORE_GID" "$@" 2>/dev/null || true
}

# report <correctness 0|1> <speedup> [<speedup> ...]
#
#   reward = 0 if correctness != 1 or geomean <= 1; otherwise geomean is
#          scored against this step's oracle baseline B: 1 at or above B,
#          ln(geomean)/ln(B) in between, clamped to [0, 1].
#   geomean = exp( mean_f( ln(max(1, speedup_f)) ) )
#
# One speedup per function in this step's scope. A function left alone
# contributes ln(1) = 0 and drags the mean down, so a big win on one function
# cannot hide doing nothing on another. max(1, .) floors a regression at 0.
# Correctness is binary and multiplies, so a tree that changes behaviour scores
# 0 however fast it is. The reward is normalized by this step's
# hardcoded oracle baseline and clamped to [0, 1].
ORACLE_BASELINE=11.3596000000
report() {
    local correct="${1:-0}"; shift || true
    local reward
    reward=$(awk -v c="$correct" -v b="$ORACLE_BASELINE" 'BEGIN {
        c += 0; if (c != 1) c = 0
        n = ARGC - 1; if (n <= 0) { printf "%.6f", 0; exit }
        s = 0
        for (i = 1; i <= n; i++) { v = ARGV[i] + 0; if (v < 1) v = 1; s += log(v) }
        g = sprintf("%.4f", exp(s / n)) + 0
        if (c != 1 || g <= 1) { printf "%.6f", 0; exit }
        if (b <= 1 || g >= b) { printf "%.6f", 1; exit }
        r = log(g) / log(b)
        if (r < 0) r = 0; if (r > 1) r = 1
        printf "%.6f", r
    }' "$@")
    local geo; geo=$(awk 'BEGIN {
        n = ARGC - 1; if (n <= 0) { printf "0.0000"; exit }
        s = 0
        for (i = 1; i <= n; i++) { v = ARGV[i] + 0; if (v < 1) v = 1; s += log(v) }
        printf "%.4f", exp(s / n)
    }' "$@")
    mkdir -p /logs/verifier
    # `similarity` is a non-scored diagnostic: the fraction of the lines the
    # candidate added that appear verbatim in upstream's file of the same name.
    # It is a verbatim-line measure, so it is a lower bound on copying and never
    # a clean bill of health. It is a number, because Harbor parses every key
    # under `rewards` as a float.
        # oracle binary step reward: 1 only above the hardcoded baseline.
    if [[ "${correct:-0}" != "1" ]] || ! awk -v g="${geo:-0}" -v b="$ORACLE_BASELINE" 'BEGIN { exit !(g > b) }'; then
        reward="0.000000"
    else
        reward="1.000000"
    fi
printf '{"reward":%s,"correctness":%s,"geomean_speedup":%s,"similarity":%s}\n' \
        "$reward" "${correct:-0}" "$geo" "${SIM_FRACTION:-0}" > /logs/verifier/reward.json
    # Reopen to 0755 on the way out so a run that collects this from the host as
    # an ordinary user can read it -- 0700 reads there as a *missing* reward. It
    # stays root-owned, so the candidate still cannot write it: writing a file
    # into a directory needs write permission on the directory itself.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    echo "REWARD correctness=${correct} speedups=[$*] geomean=${geo} reward=${reward}"
    exit 0
}

# How many repetitions each function is timed for. 301 distinct draws from the
# verifier's seed, and the median of the per-repetition times.
REPS=301

# ---------------------------------------------------------------------------
# Builds and probes. Every one of these that touches the CANDIDATE's tree goes
# through $RUN_CANDIDATE, so what it runs runs unprivileged; the verifier's own
# builds are the same functions with $RUN_CANDIDATE empty.
# ---------------------------------------------------------------------------

configure_tree() {
    # shellcheck disable=SC2086
    $RUN_CANDIDATE cmake -S "$1" -B "$2" $SRS_CMAKE_OPTS > /tmp/cmake.log 2>&1 \
        || { echo "configure failed for $1"; tail -20 /tmp/cmake.log; return 1; }
}

build_libs() {
    # shellcheck disable=SC2086
    $RUN_CANDIDATE cmake --build "$1" -j4 --target $SRS_LIB_TARGETS > /tmp/build.log 2>&1 \
        || { echo "library build failed"; tail -40 /tmp/build.log; return 1; }
}

build_tests() {
    # shellcheck disable=SC2086
    $RUN_CANDIDATE cmake --build "$1" -j4 --target $SRS_TEST_TARGETS $SRS_LIB_TARGETS > /tmp/build_tests.log 2>&1 \
        || { echo "test build failed"; tail -40 /tmp/build_tests.log; return 1; }
}

# build_harness <source-tree> <build-dir> <out-binary>
# The harness is compiled against that tree's OWN headers and static libraries,
# so it calls whatever implementation that tree installed.
build_harness() {
    $RUN_CANDIDATE g++ -std=gnu++17 -O2 -fno-rtti -march=x86-64-v3 -mtune=generic -DASSERTS_ENABLED \
        -I "$1/include" -I "$1/external/fmt/include" -I "$1/external" \
        -o "$3" /tests/vendor/bench_phy.cpp \
        -Wl,--start-group \
        "$2/lib/phy/upper/channel_coding/ldpc/libsrsran_ldpc.a" \
        "$2/lib/phy/upper/channel_coding/libsrsran_channel_coding.a" \
        "$2/lib/phy/upper/channel_coding/libsrsran_crc_calculator.a" \
        "$2/lib/phy/upper/channel_coding/polar/libsrsran_polar.a" \
        "$2/lib/phy/upper/channel_coding/short/libsrsran_short_block.a" \
        "$2/lib/phy/upper/channel_modulation/libsrsran_channel_modulation.a" \
        "$2/lib/phy/generic_functions/precoding/libsrsran_channel_precoder.a" \
        "$2/lib/ofh/compression/libsrsran_ofh_compression.a" \
        "$2/lib/phy/upper/liblog_likelihood_ratio.a" \
        "$2/lib/srsvec/libsrsvec.a" \
        "$2/lib/srslog/libsrslog.a" \
        "$2/lib/support/libsrsran_support.a" \
        "$2/lib/ran/libsrsran_ran.a" \
        "$2/external/fmt/libfmt.a" \
        /usr/lib/x86_64-linux-gnu/libyaml-cpp.so.0.7.0 \
        -Wl,--end-group \
        -lmbedtls -lmbedx509 -lmbedcrypto -lpthread -lm \
        > /tmp/harness.log 2>&1 \
        || { echo "harness build failed for $1"; tail -20 /tmp/harness.log; return 1; }
}

# Non-scored similarity diagnostic: of the lines the candidate added relative
# to the TASK's base -- the tree every step starts from, so the measure covers
# the whole cumulative work and not just this step's marginal novelty -- what
# fraction appears verbatim in upstream's file of the same name. Reads ~1.0 for
# the oracle and ~0.0 for a no-op.
compute_similarity() {
    local l0="$1" up="$2" cand="$3"
    local total=0 match=0 rel added n m line
    # One tree walk, not one diff per file: the candidate tree holds a few
    # thousand sources and only the ones that differ from the task's base can
    # contribute anything.
    while IFS= read -r line; do
        case "$line" in
            "Files "*)
                rel="${line#Files }"; rel="${rel%% and *}"; rel="${rel#$l0/}"
                [ "$rel" = "$line" ] && continue
                ;;
            "Only in $cand"*)
                rel="${line#Only in $cand}"; rel="${rel#: }"; rel="${rel#/}"
                ;;
            *) continue ;;
        esac
        case "$rel" in
            *.cpp|*.h|*.hpp|*.cc) ;;
            *) continue ;;
        esac
        added=$(diff "$l0/$rel" "$cand/$rel" 2>/dev/null | grep -E '^> ' | sed -e 's/^> //')
        [ -z "$added" ] && continue
        n=$(printf '%s\n' "$added" | grep -c . || true)
        total=$((total + n))
        [ -f "$up/$rel" ] || continue
        m=$(printf '%s\n' "$added" | awk '
            NR == FNR { u[$0] = 1; next }
            (length($0) > 0) && ($0 in u) { c++ }
            END { print c + 0 }' "$up/$rel" -)
        match=$((match + m))
    done < <(diff -rq "$l0" "$cand" 2>/dev/null)
    if [ "$total" -eq 0 ]; then
        echo "0"
    else
        awk -v m="$match" -v t="$total" 'BEGIN { printf "%.4f", m / t }'
    fi
}

# The project's own suite, as buildable in this configuration. Upstream's PHY
# test vectors are not published with the repository, so the LDPC and OFH
# compression gtests are not built here; what is built is judged relative to
# this step's base, which is what a stripped tree is allowed to fail.
# Prints the names of the tests that failed. Exits non-zero if ctest ran no
# test at all, which is a different thing from a clean run and must not read as
# one: a gate that reports "no new failures" over an empty run is
# indistinguishable from a gate that passed.
run_project_tests() {
    $RUN_CANDIDATE ctest --test-dir "$1" -R 'channel_precoder_test|dft_processor_test|dft_processor_ci16_test|evm_calculator_test' \
        > /tmp/ctest.log 2>&1 || true
    local nrun
    nrun=$(grep -cE '^[0-9]+/[0-9]+ Test +#' /tmp/ctest.log || true)
    echo "  ctest ran $nrun test(s)" >&2
    if [ "${nrun:-0}" -eq 0 ]; then
        echo "CTEST-RAN-NOTHING" >&2
        return 1
    fi
    sed -n '/tests FAILED/,$p' /tmp/ctest.log \
        | grep -oE '^[[:space:]]+[0-9]+ - [^ ]+' | awk '{print $3}' | sort -u
}
