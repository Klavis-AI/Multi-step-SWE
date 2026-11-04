#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root.  Byte-identical
# in every step, which is why it needs no per-step substitution guard.
set -u

# Unprivileged identity for candidate code.  nobody:nogroup already exists in
# debian:bookworm-slim.
SCORE_UID=65534
SCORE_GID=65534

# Lock the reward directory to root.  Call this first in test.sh.
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
# directory whatever its mode.  Use it everywhere candidate code runs -- a
# Makefile, a linked library's static initializer, a probe compiled against
# candidate source -- not just a service.
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
ORACLE_BASELINE=2.2999000000
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
    # similarity_verbatim / similarity_verbatim_tree are non-scored diagnostics
    # (see tests/vendor/similarity.py): of the lines the candidate added, the
    # fraction that is verbatim upstream's, compared per file and against
    # upstream's whole source tree.  Both are lower bounds on copying, never a
    # clean bill of health.  Plain ASCII digits only -- Harbor parses every
    # value under `rewards` as a float, and one string field aborts the
    # remaining steps.
    local sim="${SIMILARITY:-0}"
    case "${sim:-x}" in ''|*[!0-9.]*) sim=0 ;; esac
    local simtree="${SIMILARITY_TREE:-0}"
    case "${simtree:-x}" in ''|*[!0-9.]*) simtree=0 ;; esac
    mkdir -p /logs/verifier
        # oracle binary step reward: 1 only above the hardcoded baseline.
    if [[ "${correct:-0}" != "1" ]] || ! awk -v g="${geo:-0}" -v b="$ORACLE_BASELINE" 'BEGIN { exit !(g > b) }'; then
        reward="0.000000"
    else
        reward="1.000000"
    fi
printf '{"reward":%s,"correctness":%s,"geomean_speedup":%s,"similarity_verbatim":%s,"similarity_verbatim_tree":%s}\n' \
        "$reward" "${correct:-0}" "$geo" "$sim" "$simtree" > /logs/verifier/reward.json
    # Reopen to 0755 so a local docker run, which reads this from the host as
    # an ordinary user, can collect it.  Still root-owned, so the candidate
    # still cannot write it.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    echo "REWARD correctness=${correct} speedups=[$*] geomean=${geo} reward=${reward}"
    exit 0
}

# build_tree <srctree> <builddir>
#
# Candidate-only, so it always runs unprivileged: the configure step executes
# the candidate's CMakeLists.txt and the build executes whatever it generates,
# neither of which is the verifier's code.  Call give_app_to_unprivileged and
# prepare_writable_dirs first, or the build cannot create its output.
build_tree() {
    local src="$1" bld="$2"
    run_unprivileged cmake -S "$src" -B "$bld" -DCMAKE_BUILD_TYPE=Release -DSIMDUTF_TESTS=ON \
        -DSIMDUTF_BENCHMARKS=OFF -DSIMDUTF_ALWAYS_INCLUDE_FALLBACK=ON \
        > /tmp/verifier/cmake.log 2>&1 \
        || { echo "configure failed for $src"; tail -20 /tmp/verifier/cmake.log; return 1; }
    run_unprivileged cmake --build "$bld" -j4 > /tmp/verifier/build.log 2>&1 \
        || { echo "build failed for $src"; tail -30 /tmp/verifier/build.log; return 1; }
}

# Prepare /tmp for the unprivileged build, and for the verifier's own files.
#
# Two jobs, both of them about who may create a path:
#
#   /tmp/verifier  holds everything the VERIFIER writes (its logs, its own
#                  build outputs, the base dump).  It is root-owned 0700 and is
#                  created before any candidate code runs, because anything
#                  root writes at a path in world-writable /tmp can be
#                  pre-created by the candidate as a symlink to somewhere else
#                  -- root then writes THROUGH it.  Pre-creating the dump file
#                  as a link to the candidate's own output would make the
#                  per-function comparison compare a file with itself, and a
#                  link aimed at the base binaries would move the denominator.
#   the build roots  are the candidate's; nobody must own them.  Nothing runs
#                  as nobody before this function, and each path is removed
#                  first so a link already at one of them cannot turn the chown
#                  into a reach outside /tmp.
#
# /tmp/p_cand and /tmp/h_cand are NOT build roots: they are the g++ `-o`
# OUTPUT FILES of build_pipeline and build_harness (test.sh passes them as the
# <out> argument).  Creating them here as directories made every candidate
# build fail with `ld: cannot open output file /tmp/p_cand: Is a directory`,
# which reads as every candidate scoring zero.  They are removed -- never
# created -- here; the unprivileged g++ writes them into world-writable /tmp
# itself, exactly as it writes the verifier-owned /tmp/verifier/p_base, and no
# pre-creation is needed for a path the verifier's own build produces.
# Removal is `-rf`, not `-f`, so a stale directory left behind by the revision
# that created one cannot survive this function.
prepare_writable_dirs() {
    rm -rf /tmp/verifier
    mkdir -p /tmp/verifier
    chmod 700 /tmp/verifier
    rm -rf /tmp/cand /tmp/nb /tmp/p_cand /tmp/h_cand
    mkdir -p /tmp/cand /tmp/nb
    chown -R "$SCORE_UID:$SCORE_GID" /tmp/cand /tmp/nb 2>/dev/null || true
}

# build_harness <who> <srctree> <builddir> <out>
#
#   <who> is `candidate` or `base`.  The candidate's harness is compiled as
#   nobody: g++ reads the candidate's own headers and links the candidate's
#   static library, and the binary it produces runs candidate code.  The
#   verifier's /base tree is its own, so it stays root.
#
# Compiled against that tree's own headers and static library, so the method
# pointers it calls are the ones that tree installed.
build_harness() {
    local who="$1" src="$2" bld="$3" out="$4"
    local -a run=()
    [ "$who" = candidate ] && run=(run_unprivileged)
    "${run[@]}" g++ -O2 -std=c++17 -o "$out" /tests/vendor/bench_stage.cpp \
        -I "$src/include" -I "$src/src" "$bld/src/libsimdutf.a" \
        > /tmp/verifier/harness.log 2>&1 \
        || { echo "harness build failed for $src"; tail -20 /tmp/verifier/harness.log; return 1; }
}

# build_pipeline <who> <srctree> <builddir> <out>   -- same runners as above.
build_pipeline() {
    local who="$1" src="$2" bld="$3" out="$4"
    local -a run=()
    [ "$who" = candidate ] && run=(run_unprivileged)
    "${run[@]}" g++ -O2 -std=c++17 -o "$out" /tests/vendor/pipeline.cpp \
        -I "$src/include" -I "$src/src" "$bld/src/libsimdutf.a" \
        > /tmp/verifier/pipeline.log 2>&1 \
        || { echo "pipeline build failed for $src"; tail -20 /tmp/verifier/pipeline.log; return 1; }
}

# input_for <key> <variant-dir> -> path
# Keys are the corpus names make_corpus.py writes; see tests/CORPUS.stepN.
input_for() {
    case "$1" in
        u8)     echo "$2/utf8.txt" ;;
        u8err)  echo "$2/utf8err.txt" ;;
        u8lat)  echo "$2/utf8latin1.txt" ;;
        u16le)  echo "$2/utf16le.bin" ;;
        u16be)  echo "$2/utf16be.bin" ;;
        u16bad) echo "$2/utf16bad.bin" ;;
        u32le)  echo "$2/utf32le.bin" ;;
        u16lat) echo "$2/utf16latin1.bin" ;;
        u32lat) echo "$2/utf32latin1.bin" ;;
        lat)    echo "$2/latin1.bin" ;;
        ascii)  echo "$2/ascii.txt" ;;
        bin)    echo "$2/bin.bin" ;;
        b64)    echo "$2/base64.txt" ;;
        ndl)    echo "$2/findneedle.txt" ;;
        *)      echo "" ;;
    esac
}

# median of the arguments
median_of() {
    printf '%s\n' "$@" | sort -g | awk '{a[NR]=$1} END {
        if (NR == 0) { print 0; exit }
        print (NR % 2) ? a[(NR+1)/2] : (a[NR/2] + a[NR/2+1]) / 2
    }'
}
