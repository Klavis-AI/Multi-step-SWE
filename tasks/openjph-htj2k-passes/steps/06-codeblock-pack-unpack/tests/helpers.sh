#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root.  Byte-identical
# across steps on purpose -- a shared file that drifts is a shared bug.
set -u

# --- candidate code runs unprivileged --------------------------------------
# Unprivileged identity for candidate code.  nobody:nogroup (65534:65534)
# already exists in debian:bookworm-slim, which every verifier image uses.
SCORE_UID=65534
SCORE_GID=65534

# Lock the reward directory to root.  Call this first in test.sh, after the
# step-binding check (which runs no candidate code) and before anything else.
#
# Harbor chmods /logs/verifier to 777 just before this container starts, so
# anything running here can overwrite reward.json -- including candidate code
# the verifier builds or runs.  Writing the real score last does not help: a
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
# directory whatever its mode.  Every place candidate code runs goes through
# this: the candidate's cmake configure and build (a CMakeLists executes
# arbitrary code), its test suite, the binaries it produced, and the harness
# compiled against its headers and linked against its library (a static
# initializer in that library runs there).
run_unprivileged() {
    setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        env HOME=/tmp TMPDIR=/tmp "$@"
}

# Give /app to the unprivileged user.  Call before an unprivileged build.
#
# /app arrives owned by whoever wrote the artifact.  Without this the build
# cannot create its output directory, so every candidate scores 0 and the step
# looks merely very hard instead of broken.
give_app_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" /app 2>/dev/null || true
}

# as_user <root|unprivileged> <cmd> [<arg> ...]
#
# One call site for both sides of every comparison.  Verifier-owned work (the
# base tree, /basebld, /zerobld, /tmp/h_base) stays root; every command that
# executes candidate-authored code runs through run_unprivileged.
as_user() {
    local who="$1"; shift
    if [ "$who" = unprivileged ]; then run_unprivileged "$@"; else "$@"; fi
}

# report <correctness 0|1> <speedup> [<speedup> ...]
#
#   reward = 0 if correctness != 1 or value <= 1, where value is the
#   geometric mean of max(1, speedup_f) over the functions.  Otherwise value
#   is scored against this step's oracle baseline B: 1 at or above B,
#   ln(value)/ln(B) in between, clamped to [0, 1].
#
# One speedup per function in this step's scope.  The geometric mean is the
# right aggregator for ratios, and it penalises neglect: a function left alone
# contributes ln(1) = 0 and drags the step down, so a big win on one function
# cannot hide doing nothing on another.
#
# max(1, .) floors a regression at 0 rather than going negative.  Correctness is
# a binary gate: a tree that changes behaviour scores 0 however fast.
ORACLE_BASELINE=1.5183000000
report() {
    local correct="${1:-0}"; shift || true
    # non-scored diagnostic: of the code lines the candidate added, what
    # fraction appears verbatim in upstream's implementation of this stage.
    # Read ~1.0 for the oracle and ~0.0 for a no-op.  It is a verbatim-line
    # measure, so cosmetically rewritten code reads near zero and a launderer
    # is indistinguishable from an honest reimplementer -- a lower bound on
    # copying, never a clean bill of health.
    local sim="${VERBATIM_SIMILARITY:-0}"
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
printf '{"reward":%s,"correctness":%s,"geomean_speedup":%s,"verbatim_similarity":%s}\n' \
        "$reward" "${correct:-0}" "$geo" "$sim" > /logs/verifier/reward.json
    # Reopen the directory to 0755 on the way out, so a local docker run --
    # which reads this from the host as an ordinary user -- can collect it.
    # Still root-owned, and the candidate has already had its last chance to
    # change it: a detached child cannot write a root-owned file in a
    # root-owned directory whatever it does.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    echo "REWARD correctness=${correct} speedups=[$*] geomean=${geo} reward=${reward}"
    exit 0
}

# median <n> <n> ...  -> median, printed to 4 decimals.  Used for the per
# function ratio so one contended repetition cannot become the result: best-of-N
# suppresses slow outliers but *preserves* spuriously fast ones, and a ratio
# then reports that as a speedup.
median() {
    printf '%s\n' "$@" | sort -g | awk '
        { a[NR] = $1 }
        END { if (NR == 0) { print "0"; exit }
              if (NR % 2) printf "%.4f\n", a[(NR + 1) / 2]
              else        printf "%.4f\n", (a[NR / 2] + a[NR / 2 + 1]) / 2 }'
}

# build_candidate <srctree> <builddir> [root|unprivileged]
#
# CMake configure runs whatever the tree's CMakeLists contains, so this is a
# candidate-code site: the candidate side calls it with `unprivileged`.
build_candidate() {
    local who="${3:-root}"
    as_user "$who" cmake -S "$1" -B "$2" -DCMAKE_BUILD_TYPE=Release > /tmp/cmake.log 2>&1 \
        || { echo "candidate configure failed"; tail -20 /tmp/cmake.log; return 1; }
    as_user "$who" cmake --build "$2" -j"$(nproc)" > /tmp/build.log 2>&1 \
        || { echo "candidate build failed"; tail -30 /tmp/build.log; return 1; }
}

# build_harness <srctree> <builddir> <out> [root|unprivileged]  -- compiled
# against that tree's own headers and linked against its library, so it calls
# that tree's INSTALLED implementations.  The candidate side compiles candidate
# headers and links candidate objects, so it runs unprivileged.
build_harness() {
    local who="${4:-root}"
    as_user "$who" g++ -O2 -std=c++14 -o "$3" /tests/vendor/bench_stage.cpp \
        -I "$1/src/core/openjph" -I "$1/src/core/codestream" \
        -I "$1/src/core/transform" -I "$1/src/core/coding" \
        -I "$2/src/core" -L "$2/src/core" -lopenjph \
        -Wl,-rpath,"$2/src/core" -ldl \
        > /tmp/harness.log 2>&1 \
        || { echo "harness build failed for $1"; tail -20 /tmp/harness.log; return 1; }
}

# pipeline_digests <build-dir> [root|unprivileged] -- both directions, several
# configurations.  Equality here is equality, not a tolerance: JPEG 2000 coding
# is bit-exact.  Output goes through temp files: ojph_expand rejects a
# destination with no file extension, so /dev/stdout is not usable.
#
# The candidate side is candidate code (the binaries it built), so it runs
# unprivileged; its private temp directory is handed to the unprivileged user
# first, since a root-made mktemp dir would not accept the candidate's writes.
pipeline_digests() {
    local b="$1" who="${2:-root}" ppm="/workload/heldout.ppm"
    local C="$b/src/apps/ojph_compress/ojph_compress"
    local E="$b/src/apps/ojph_expand/ojph_expand"
    local t; t=$(mktemp -d)
    [ "$who" = unprivileged ] && chown "$SCORE_UID:$SCORE_GID" "$t"
    local d=()
    as_user "$who" "$C" -i "$ppm" -o "$t/e1.j2c" -reversible true  -num_decomps 5 >/dev/null 2>&1
    as_user "$who" "$C" -i "$ppm" -o "$t/e2.j2c" -reversible false -num_decomps 5 -qstep 0.02 >/dev/null 2>&1
    as_user "$who" "$C" -i "$ppm" -o "$t/e3.j2c" -reversible true  -num_decomps 3 >/dev/null 2>&1
    as_user "$who" "$E" -i /workload/heldout_rev.j2c -o "$t/d1.ppm" >/dev/null 2>&1
    as_user "$who" "$E" -i /workload/heldout_irv.j2c -o "$t/d2.ppm" >/dev/null 2>&1
    for f in e1.j2c e2.j2c e3.j2c d1.ppm d2.ppm; do
        [ -s "$t/$f" ] && md5sum "$t/$f" | cut -d' ' -f1 || echo MISSING
    done
    rm -rf "$t"
}

digest_labels=(enc-rev5 enc-irv5 enc-rev3 dec-rev53 dec-irv97)
