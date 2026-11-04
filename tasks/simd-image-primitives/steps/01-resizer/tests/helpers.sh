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
# the runner chmods /logs/verifier to 777 just before this container starts, so
# anything running here can overwrite reward.json -- including candidate code
# the verifier builds or runs. Writing the real score last does not help: a
# detached child outlives the scorer and writes after it.
#
# This runs after the runner's chmod, so this is the mode that sticks.
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 700 /logs/verifier
}

# Run candidate-authored code as nobody instead of root.
#
# Needed for harden_reward_dir to mean anything: root can write a root-owned
# directory whatever its mode. Used for everything this step does that runs or
# is built from candidate-authored code -- the cmake configure, the build, and
# the project's own Test binary, which is linked against the candidate's
# library and whose static initializers run before main.
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
#   reward = 0 if correctness != 1 or value <= 1, where value is the
#   geometric mean of max(1, speedup_f) over the functions.  Otherwise value
#   is scored against this step's oracle baseline B: 1 at or above B,
#   ln(value)/ln(B) in between, clamped to [0, 1].
#
# One speedup per scored function. The geometric mean is the right aggregator
# for ratios, and it penalises neglect: a function left alone contributes
# ln(1) = 0 and drags the step down, so a big win on one function cannot hide
# doing nothing on another.
#
# max(1, .) floors a regression at 0 rather than going negative. Correctness is
# a binary gate: a tree that changes behaviour scores 0 however fast.
ORACLE_BASELINE=3.1780000000
report() {
    local correct="${1:-0}"; shift || true
    local reward
    reward=$(awk -v c="$correct" -v B="$ORACLE_BASELINE" 'BEGIN {
        c += 0; if (c != 1) { printf "%.6f", 0; exit }
        n = ARGC - 1; if (n <= 0) { printf "%.6f", 0; exit }
        s = 0
        for (i = 1; i <= n; i++) { v = ARGV[i] + 0; if (v < 1) v = 1; s += log(v) }
        g = sprintf("%.4f", exp(s / n)) + 0
        if (g <= 1) { printf "%.6f", 0; exit }
        if (B <= 1) { printf "%.6f", 1; exit }
        if (g >= B) { printf "%.6f", 1; exit }
        r = log(g) / log(B)
        if (r < 0) r = 0; if (r > 1) r = 1
        printf "%.6f", r
    }' "$@")
    local geo
    geo=$(awk 'BEGIN {
        n = ARGC - 1; if (n <= 0) { printf "%.4f", 1; exit }
        s = 0
        for (i = 1; i <= n; i++) { v = ARGV[i] + 0; if (v < 1) v = 1; s += log(v) }
        printf "%.4f", exp(s / n)
    }' "$@")
    mkdir -p /logs/verifier
    # oracle binary step reward: 1 only above the hardcoded baseline.
    if [[ "${correct:-0}" != "1" ]] || ! awk -v g="${geo:-0}" -v b="$ORACLE_BASELINE" 'BEGIN { exit !(g > b) }'; then
        reward="0.000000"
    else
        reward="1.000000"
    fi
    printf '{"reward":%s,"correctness":%s,"geomean_speedup":%s,"functions":%d}\n' \
        "$reward" "${correct:-0}" "$geo" "$#" > /logs/verifier/reward.json
    # Reopen to 0755 so a local docker run, which reads this from the host as
    # an ordinary user, can collect it. Still root-owned, so the candidate
    # still cannot write it.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    echo "REWARD correctness=${correct} functions=$# geomean=${geo} reward=${reward}"
    exit 0
}

# build_candidate <source tree> <build dir>
#
# Candidate code: the configure step runs the tree's own CMake files and the
# build compiles candidate sources, so both run unprivileged. The caller has
# already handed the copied tree and the build dirs to the unprivileged user.
build_candidate() {
    run_unprivileged cmake -G Ninja -S "$1/prj/cmake" -B "$2" \
        -DCMAKE_BUILD_TYPE=Release -DSIMD_TEST=ON -DSIMD_PYTHON=OFF \
        -DSIMD_SYNET=OFF -DSIMD_AVX512=OFF -DSIMD_AVX512VNNI=OFF \
        > /tmp/cmake.log 2>&1 || { echo "candidate configure failed"; tail -20 /tmp/cmake.log; return 1; }
    run_unprivileged cmake --build "$2" -j"$(nproc)" > /tmp/build.log 2>&1 \
        || { echo "candidate build failed"; tail -30 /tmp/build.log; return 1; }
}

# score_reports <funcs-file> <report> ...
#   Per scored function: the mean over its matching rows within each report,
#   then the MEDIAN across reports.  Echoes "<func>\t<api_ms>\t<base_ms>\t<sp>".
score_reports() {
    python3 /tests/vendor/score_reports.py "$@"
}

# check_envelope <base-tree> <cand-tree> <scope-file> <envelope-file> <build-dir>
#   ISA-envelope gate, scoped to what this step's candidate changed.  The
#   instruction limits this family to SSE4.1 through AVX2; this holds the files
#   the step owns (plus the dispatch file) to it, on the lines the candidate
#   added or changed relative to the base tree it is given, and against the
#   graded build's own compiled-source list.  The base tree the verifier passes
#   is the pinned release with all eight families removed (the ladder's L0), so
#   at step k those lines also include the candidate's own earlier steps in the
#   files this step shares with them -- a line that is out of envelope there
#   would already have failed the step that wrote it, since SimdLib.cpp is in
#   every step's scope.  Runs as root: it only reads and compares text, it does
#   not execute candidate code.  Files the candidate left alone are reported as
#   unchanged and skipped, so an untouched tree passes vacuously and visibly
#   rather than being failed for code its candidate never wrote.
#   vendor/envelope_scan.py is the scanner.
check_envelope() {
    python3 /tests/vendor/envelope_scan.py "$@"
}
