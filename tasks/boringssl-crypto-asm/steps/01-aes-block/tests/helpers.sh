#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Keep in sync.
set -u

# --- protecting the reward from the candidate ------------------------------
#
# Both halves below are needed. harden_reward_dir takes the directory back to
# root:root 0700 first thing in test.sh, so that mode is the one that sticks --
# but root can write a root-owned directory whatever its mode, so every place
# candidate code executes also runs through run_unprivileged, and the build
# tree is handed to that user first.

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

# This verifier never executes anything out of /app: the candidate's tree is
# copied to /src first and the build runs there, so the tree that has to be
# handed over is /src and the build directory beside it -- and that is what
# build_candidate_incremental does, below, before it starts the build.
# give_app_to_unprivileged is the template's version of the same step, for a
# verifier that builds /app in place; it is kept here because this helpers.sh
# is a copy of the template's and the next editor should not have to rediscover
# which of the two applies.

# similarity diagnostic: of the code lines the candidate added, what fraction
# appears verbatim in upstream's version of the same file.
#
#   prints "<matched> <total>", or returns non-zero if it cannot be computed
#
# Upstream's side is a sorted set of "<path> <sha1 of the normalised line>" that
# the image build made from the pinned checkout *before* the stripper ran (see
# the Dockerfile for why the order matters). The candidate's side is the added
# lines of every file rsync reported as changed, normalised and hashed by the
# same /tests/vendor/code_lines.py, so the two sides cannot normalise differently.
#
# Reported alongside the reward, never inside it. No gate is possible here: the
# reference solution IS upstream, so any threshold that caught a transcriber
# would fail the oracle first. Read it as a lower bound on copying.
#
# Both counts are printed, because the fraction alone cannot distinguish "added
# nothing" from "added plenty and none of it matched".
_similarity() {
    local list=/tmp/rsync-changed.txt
    local up=/upstream_code_lines.step$SID
    [ -d /src ] && [ -d /base ] && [ -s "$up" ] || return 1

    : > /tmp/sim_cand.txt
    if [ -s "$list" ]; then
        local _mode path
        while read -r _mode path; do
            [ -n "${path:-}" ] || continue
            [ -f "/src/$path" ] || continue
            # -N so a file the candidate created is all-added rather than an
            # error; the "> " prefix diff puts on an added line is stripped
            # before hashing, since it is not part of the line.
            diff -N -- "/base/$path" "/src/$path" 2>/dev/null \
                | sed -n 's/^> //p' \
                | python3 /tests/vendor/code_lines.py --lang "$path" - \
                | sed "s|^|$path |" >> /tmp/sim_cand.txt
        done < "$list"
    fi
    LC_ALL=C sort -u /tmp/sim_cand.txt -o /tmp/sim_cand.txt
    awk 'NR == FNR { up[$0]; next }
         { t++; if ($0 in up) m++ }
         END { printf "%d %d\n", m + 0, t + 0 }' "$up" /tmp/sim_cand.txt
}

# report <correctness 0|1> <speedup> [<speedup> ...]
#
#   reward = correctness * mean_f( ln(max(1, speedup_f)) )
#          = correctness * ln( geometric mean of the per-function speedups )
#
# One speedup per function in this milestone's scope. The geometric mean is the
# right aggregator for ratios, and it penalises neglect: a function left alone
# contributes ln(1) = 0 and drags the milestone down, so a big win on one
# function cannot hide doing nothing on another.
#
# max(1, .) floors a regression at 0 rather than going negative. Correctness is
# binary and multiplies, so a tree that changes behaviour scores 0 however fast.
ORACLE_BASELINE=24.6985000000
report() {
    local correct="${1:-0}"; shift || true
    local reward
    reward=$(awk -v c="$correct" -v b="$ORACLE_BASELINE" 'BEGIN {
        c += 0; b += 0
        if (c != 1) { printf "%.6f", 0; exit }
        n = ARGC - 1; if (n <= 0) { printf "%.6f", 0; exit }
        s = 0
        for (i = 1; i <= n; i++) { v = ARGV[i] + 0; if (v < 1) v = 1; s += log(v) }
        g = sprintf("%.4f", exp(s / n)) + 0
        if (g <= 1) { printf "%.6f", 0; exit }
        if (b <= 1 || g >= b) { printf "%.6f", 1; exit }
        r = log(g) / log(b)
        if (r < 0) r = 0; if (r > 1) r = 1
        printf "%.6f", r
    }' "$@")
    local geo; geo=$(awk 'BEGIN {
        n = ARGC - 1; if (n <= 0) { printf "%.4f", 1; exit }
        s = 0
        for (i = 1; i <= n; i++) { v = ARGV[i] + 0; if (v < 1) v = 1; s += log(v) }
        printf "%.4f", exp(s / n)
    }' "$@")
    # Non-scored. It cannot move $reward or $correct, and a failure to compute it
    # reports null rather than 0 -- "no added lines to measure" and "could not
    # measure" must not look alike in the log.
    local sim frac=null lines=null added=null
    sim=$(_similarity 2>/dev/null) || sim=""
    if [ -n "$sim" ]; then
        lines=${sim%% *}; added=${sim##* }
        frac=$(awk -v m="$lines" -v t="$added" \
            'BEGIN { if (t + 0 <= 0) printf "0.0"; else printf "%.4f", m / t }')
    fi
    mkdir -p /logs/verifier
        # oracle binary step reward: 1 only above the hardcoded baseline.
    if [[ "${correct:-0}" != "1" ]] || ! awk -v g="${geo:-0}" -v b="$ORACLE_BASELINE" 'BEGIN { exit !(g > b) }'; then
        reward="0.000000"
    else
        reward="1.000000"
    fi
printf '{"reward":%s,"correctness":%s,"geomean_speedup":%s,"upstream_verbatim_fraction":%s,"upstream_verbatim_lines":%s,"upstream_added_lines":%s}\n' \
        "$reward" "${correct:-0}" "$geo" "$frac" "$lines" "$added" > /logs/verifier/reward.json
    # Reopen to 0755 so a local docker run, which reads this from the host as
    # an ordinary user, can collect it. Still root-owned, so the candidate
    # still cannot write it -- harden_reward_dir took it to 0700 and every
    # place candidate code runs goes through run_unprivileged.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    echo "REWARD correctness=${correct} speedups=[$*] geomean=${geo} reward=${reward}"
    echo "SIMILARITY upstream_verbatim_fraction=${frac} (${lines} of ${added} added code lines verbatim upstream)"
    exit 0
}

# Build a BoringSSL tree. `crypto` is the library the harness links and
# `crypto_test` is the project's own suite; one target list builds both.
#
# `runner` is empty for a verifier-owned tree (the milestone's base and the
# ladder's L0 floor, both built into this image at image-build time) and
# `run_unprivileged` for the candidate's: configuring a CMake tree executes the
# CMakeLists, and the CMakeLists here is one the candidate delivered. -j4 is a
# cap on both sides; see the note below.
build_tree() {
    local src="$1" bld="$2" runner="${3:-}"
    $runner cmake -S "$src" -B "$bld" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        > /tmp/cmake.log 2>&1 || { echo "configure failed for $src"; tail -20 /tmp/cmake.log; return 1; }
    $runner cmake --build "$bld" -j4 --target crypto crypto_test > /tmp/build.log 2>&1 \
        || { echo "build failed for $src"; tail -40 /tmp/build.log; return 1; }
}

# Every build in this image runs at -j4. The host is shared, and a 16-wide build
# from one grading container starves every other job on the box -- including the
# measurements this container is about to take.

# A candidate may leave a build directory inside its source tree; copying that
# into the reference tree would both waste the copy and confuse the rebuild.
COPY_EXCLUDES=(--exclude=/build/ --exclude=/.git/)
# -rl, never -a: see build_candidate_incremental.

# Refresh the workspace tree at /src from the candidate's, then build it into a
# FRESH directory. The copy is minimal (rsync -c, checksums not timestamps) so
# only what the candidate actually changed moves; the build is clean, because an
# incremental one on top of the image's reference build directory is not
# reliable and a wrong measurement is worse than a slow one.
#
# Measured, and the reason this is not incremental: rsync preserves mtimes, and
# ninja compares them, so files brought in by the copy whose recorded time is
# older than the object already present are silently not recompiled -- the
# verifier then reports "built" while measuring the BASE library. Reproduced on
# this host with a 126-file change: AES read 2061 ns instead of 38, and the
# build exited 1 on "manifest 'build.ninja' still dirty after 100 tries".
# Dropping rsync's -t was not enough, so the rebuild is made independent of it.
#
# `rsync --out-format` also yields the changed-file list the capability-envelope
# check needs; nothing else in the verifier knows the changed set.
#
# /src is also the tree the unprivileged build runs in -- the build executes the
# candidate's CMakeLists -- so it is re-created as an independent copy of /base
# before the rsync lands the candidate's changes on it, and handed over to the
# build user; see the note in the function.
build_candidate_incremental() {
    local src="$1"
    # /src is the image's hardlink farm of /base (`cp -al`), and the build
    # below runs as nobody, so /src has to be handed over -- but a chown of a
    # hardlinked file reaches /base's own inode through the shared link, and
    # /base is verifier-owned: it is the correctness reference and the timing
    # denominator, and candidate code must not be able to write it. So /src is
    # given its own inodes first. It shares nothing with /base afterwards, and
    # the cost is one local copy of the tree.
    [ -d /base ] || { echo "missing /base: this verifier image has no base tree"; return 1; }
    rm -rf /src
    cp -a /base /src || { echo "could not copy /base to /src"; return 1; }
    rsync -rl -c --delete "${COPY_EXCLUDES[@]}" --out-format='%i %n' \
        "$src"/ /src/ > /tmp/rsync-changed.txt 2>/tmp/rsync.log \
        || { echo "rsync failed"; tail -20 /tmp/rsync.log; return 1; }
    rm -rf /candbld
    mkdir -p /candbld
    # Hand both directories to the unprivileged builder before it starts:
    # /candbld is the configure's output directory and was just created by
    # root, which nobody cannot write into; /src is where the build reads, and
    # where any generated file it writes back would go.
    chown -R "$SCORE_UID:$SCORE_GID" /src /candbld 2>/dev/null || true
    build_tree /src /candbld run_unprivileged || return 1
    echo "  candidate built clean into /candbld"
    return 0
}

# build_harness <includedir> <libcrypto.a> <out> [runner]
#
# The library is C++, so the harness is compiled as C and linked with the C++
# driver. It is built against each tree's own headers and static archive, so it
# calls the implementation that tree installed.
#
# `runner` is empty for the two verifier-owned trees (the milestone's base and
# the ladder's L0 floor) and `run_unprivileged` for the candidate's: the
# compile reads the candidate's headers and the link reads the candidate's
# archive, and what comes out calls the candidate's library. The object file is
# named after the output rather than shared, so the root-owned object of a
# verifier-owned build cannot block the unprivileged one.
build_harness() {
    local inc="$1" lib="$2" out="$3" runner="${4:-}"
    local obj="/tmp/$(basename "$out").o"
    $runner gcc -O2 -std=c11 -I "$inc" -c /tests/vendor/bench_stage.c -o "$obj" \
        > /tmp/harness.log 2>&1 \
        || { echo "harness compile failed"; tail -20 /tmp/harness.log; return 1; }
    $runner g++ -o "$out" "$obj" "$lib" -lpthread >> /tmp/harness.log 2>&1 \
        || { echo "harness link failed"; tail -20 /tmp/harness.log; return 1; }
}
