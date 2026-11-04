#!/usr/bin/env bash
# Shared verifier helpers.
#
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root.  Keep in sync.
set -u

# Unprivileged identity for candidate code.  nobody:nogroup already exists in
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
# directory whatever its mode. Use it everywhere candidate code runs -- a
# Makefile, a linked library's static initializer, a probe compiled against
# candidate source -- not just a service.
run_unprivileged() {
    setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        env HOME=/tmp TMPDIR=/tmp "$@"
}

# Give /app to the unprivileged user. Call before an unprivileged build.
#
# /app arrives owned by whoever wrote the artifact. The candidate's tree is
# copied out of it before it is built, so the copy inherits this ownership, and
# the candidate's own applications and unit tests run with /app as their working
# directory, so it has to be writable by that identity.  Nothing below reads
# /app after the copy: the verifier's own tree is /base, which stays root.
give_app_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" /app 2>/dev/null || true
}

# report <correctness 0|1> <speedup> [<speedup> ...]
#
#   reward = 1 if correctness == 1 and geomean > this step's oracle baseline B;
#          otherwise 0.
#   geomean = exp( mean_f( ln(max(1, speedup_f)) ) )
#
# One speedup per function in this step's scope.  The geometric mean is the
# right aggregator for ratios, and it penalises neglect: a function left alone
# contributes ln(1) = 0 and drags the step down, so a large win on one function
# cannot hide doing nothing on another.
#
# max(1, .) floors a regression at 0 rather than going negative.  Correctness
# is binary and multiplies, so a tree that changes behaviour scores 0 however
# fast it is.
#
# The reward is binary against this step's hardcoded oracle baseline.  the runner
# reads the "reward" key; every key under it must be numeric.
ORACLE_BASELINE=1.9303000000
report() {
    local correct="${1:-0}"; shift || true
    local reward
    local geo; geo=$(awk 'BEGIN {
        n = ARGC - 1; if (n <= 0) { printf "0.0000"; exit }
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
    # Reopen the directory to 0755 on the way out, as the house helpers do: a
    # local docker run collects the reward from the host as an ordinary user,
    # where harden_reward_dir's 0700 reads as a *missing* reward.  The
    # directory and the file stay root-owned, so the candidate still cannot
    # write them -- a 0755 root-owned directory is not writable by anyone else.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    echo "REWARD correctness=${correct} speedups=[$*] geomean=${geo} reward=${reward}"
    exit 0
}

# A verifier-side fault (missing tree, unbuildable candidate, harness that
# cannot be linked) is reported the same way but says so, so that a zero is
# never mistaken for a candidate's measurement.
fail() {
    mkdir -p /logs/verifier
    # Numbers only: the runner parses every value in reward.json as a float, and a
    # string field aborts the remaining steps.  The fault's name goes to stderr
    # (the runner records it with the trial), not into this file.
    printf '{"reward":0,"correctness":0,"geomean_speedup":0,"functions":0}\n' \
        > /logs/verifier/reward.json
    # Same reopen as report(): the host reads this back as an ordinary user.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    echo "VERIFIER FAULT: ${1:-unknown}"
    exit 0
}

# ---------------------------------------------------------------- build ---
# build_tree <tree> <bld>
#
# The candidate's tree.  cmake reads the candidate's CMakeLists.txt -- which
# executes arbitrary code at configure time -- and ninja runs the commands that
# configure generated, so both run unprivileged.  <tree> and <bld> must already
# belong to the unprivileged identity (test.sh hands them over); the two logs
# are opened by this root shell, so they do not have to.
build_tree() {
    run_unprivileged cmake -S "$1" -B "$2" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
        -DBUILD_TESTING=ON \
        -DCMAKE_OUTPUT_DIRECTORY="$2/out" > "$2.cmake.log" 2>&1 || {
        echo "candidate configure failed"; tail -25 "$2.cmake.log"; return 1; }
    run_unprivileged ninja -C "$2" >> "$2.cmake.log" 2>&1 || {
        echo "candidate build failed"; grep -m25 -E "error" "$2.cmake.log"; return 1; }
    return 0
}

# ------------------------------------------------------------- harness ---
# The harness is compiled outside the tree's build system and linked against
# that tree's own object files, so it necessarily calls the implementation the
# tree installed for each slot.
#
# For a candidate tree this compile consumes candidate objects and candidate
# headers and runs the compiler's driver over them, so it runs unprivileged as
# well.  <out> must therefore sit in a directory that identity can write; the
# .log is opened by this root shell.
build_harness_for() {
    local tree=$1 bld=$2 out=$3
    local incs="" d
    for d in "$tree/Source/API" \
             "$tree/Source/Lib/Common/Codec" "$tree/Source/Lib/Common/ASM_SSE2" "$tree/Source/Lib/Common/ASM_AVX2" \
             "$tree/Source/Lib/Encoder/Codec" "$tree/Source/Lib/Encoder/ASM_SSE2" "$tree/Source/Lib/Encoder/ASM_SSE4_1" "$tree/Source/Lib/Encoder/ASM_AVX2" \
             "$tree/Source/Lib/Decoder/Codec" "$tree/Source/Lib/Decoder/ASM_SSE4_1" "$tree/Source/Lib/Decoder/ASM_AVX2" \
             "$tree/third_party/cpuinfo/include"; do
        [ -d "$d" ] && incs="$incs -I$d"
    done
    local objs; objs=$(find "$bld/Source/Lib" "$bld/third_party/cpuinfo" -name '*.o' 2>/dev/null | sort)
    [ -n "$objs" ] || { echo "no objects under $bld"; return 1; }
    run_unprivileged gcc -O2 -std=gnu99 -D_GNU_SOURCE -o "$out" \
        /tests/vendor/bench/bench_main.c /tests/vendor/bench/bench_stage_*.c \
        $incs $objs -lpthread -lm -Wl,-z,noexecstack > "$out.log" 2>&1 || {
        echo "harness link failed for $tree"; tail -15 "$out.log"; return 1; }
    return 0
}

# ------------------------------------------------------------- digests ---
et_configs() {
    printf '%s\n' \
      "yuv422 8 320 240" \
      "yuv420 8 320 240" \
      "yuv444 8 320 240" \
      "yuv422 10 320 240" \
      "yuv420 10 320 240" \
      "yuv444 10 320 240"
}

# et_digests <tree-build-output-dir> [unprivileged] -- the six end-to-end
# configurations, each encoded AND decoded, one md5 per line.  Bit-exactness is
# the invariant, so these are equality checks, not tolerances.
#
# The base tree's applications run as root; the candidate's are linked against
# the candidate's library and run unprivileged.  The two scratch files are
# removed before each use: they are written by root on one side and by nobody on
# the other, and a stale file would either fail that write or leave a failed
# encode to be read back as the other tree's digest.
et_digests() {
    local out=$1
    local -a runner=()
    [ "${2:-}" = "unprivileged" ] && runner=(run_unprivileged)
    local fmt depth w h
    while read -r fmt depth w h; do
        [ -z "$fmt" ] && continue
        local src="/workload/in_${fmt}_${depth}.yuv"
        rm -f /tmp/et.jxs /tmp/et_dec.yuv
        "${runner[@]}" "$out/SvtJpegxsEncApp" -i "$src" -w "$w" -h "$h" --colour-format "$fmt" \
            --input-depth "$depth" --bpp 3 -n 4 --lp 0 -b /tmp/et.jxs --no-progress 1 >/dev/null 2>&1
        md5sum /tmp/et.jxs | cut -d' ' -f1
        "${runner[@]}" "$out/SvtJpegxsDecApp" -i /tmp/et.jxs -o /tmp/et_dec.yuv -n 4 --lp 0 >/dev/null 2>&1
        md5sum /tmp/et_dec.yuv | cut -d' ' -f1
    done < <(et_configs)
}

# One encode of the guard workload, in milliseconds, on stdout.
#
# <out> holds the tree's own SvtJpegxsEncApp; the candidate's side is the one
# that runs unprivileged.  The bitstream is removed first and named per side:
# root writes one and nobody the other, and a file left by the other side would
# either fail the write or hide a failed encode.
encode_once() {
    local out=$1
    local -a runner=()
    [ "${2:-}" = "unprivileged" ] && runner=(run_unprivileged)
    local jxs="/tmp/guard.$$.${2:-root}.jxs"
    rm -f "$jxs"
    "${runner[@]}" "$out/SvtJpegxsEncApp" -i /workload/guard.yuv -w 640 -h 480 --colour-format yuv422 \
        --input-depth 8 --bpp 3 -n 24 --lp 0 -b "$jxs" --no-progress 1 2>&1 \
        | grep -o 'total encoding [0-9.]*' | grep -o '[0-9.]*$'
}

# best_of <a> <b> -- the smaller positive value, or the non-zero one.
best_of() {
    awk -v a="$1" -v b="$2" 'BEGIN{
        if (a+0 <= 0) { print b+0; exit }
        if (b+0 <= 0) { print a+0; exit }
        print (a < b) ? a : b
    }'
}

# Wall clock of one encode, best of three PAIRS of alternating runs.
#
# Whole-program wall clock is one-sided noise, so best-of is the right
# aggregator here -- but only if the two sides are measured under the same load.
# This is a shared host: back-to-back measurements of the same tree minutes
# apart were seen to differ by 55% (an L0 encode read 0.85 ms in one run and
# 0.48 ms in another).  Alternating base, candidate, base, candidate ... and
# taking the best of each side makes the two numbers see the same load, which is
# what the comparison needs.  Prints "<base_ms> <cand_ms>".
#
# The second argument is always the candidate's tree: it runs unprivileged, the
# verifier-owned base tree runs as root.
guard_pair() {
    local base_out=$1 cand_out=$2 b="" c="" t
    local i
    for i in 1 2 3; do
        t=$(encode_once "$base_out"); b=$(best_of "$b" "$t")
        t=$(encode_once "$cand_out" unprivileged); c=$(best_of "$c" "$t")
    done
    echo "${b:-0} ${c:-0}"
}
