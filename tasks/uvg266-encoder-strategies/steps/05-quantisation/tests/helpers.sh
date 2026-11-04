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
#   reward = 1 if correctness == 1 and geomean > this step's oracle baseline B;
#          otherwise 0.
#   geomean = exp( mean_f( ln(max(1, speedup_f)) ) )
#
# One speedup per function in this step's scope. The geometric mean is the
# right aggregator for ratios, and it penalises neglect: a function left alone
# contributes ln(1) = 0 and pulls the step down, so a large win on one function
# cannot hide doing nothing on another. max(1, .) floors a regression at zero
# rather than letting it go negative.
#
# The reward is binary against this step's hardcoded oracle baseline: it is 1
# only when the geomean is above that baseline.
ORACLE_BASELINE=2.2538000000
report() {
    local correct="${1:-0}"; shift || true
    local reward geomean
    geomean=$(awk 'BEGIN {
        n = ARGC - 1; if (n <= 0) { printf "0.0000"; exit }
        s = 0
        for (i = 1; i <= n; i++) { v = ARGV[i] + 0; if (v < 1) v = 1; s += log(v) }
        printf "%.4f", exp(s / n)
    }' "$@")
    mkdir -p /logs/verifier
        # oracle binary step reward: 1 only above the hardcoded baseline.
    if [[ "${correct:-0}" != "1" ]] || ! awk -v g="${geomean:-0}" -v b="$ORACLE_BASELINE" 'BEGIN { exit !(g > b) }'; then
        reward="0.000000"
    else
        reward="1.000000"
    fi
printf '{"reward":%s,"correctness":%s,"geomean_speedup":%s}\n' \
        "$reward" "${correct:-0}" "$geomean" > /logs/verifier/reward.json
    # Merge the non-scored diagnostics. the runner parses every key under `rewards`
    # as a float, so only numbers may be added; a string field kept "for
    # readability" aborted a trial in a shipped task.
    if [ -f /logs/verifier/verbatim_line_fraction.txt ]; then
        python3 -I - <<'PY' || true
import json
d = json.load(open("/logs/verifier/reward.json"))
try:
    d["verbatim_line_fraction"] = float(
        open("/logs/verifier/verbatim_line_fraction.txt").read().strip())
except ValueError:
    pass
json.dump(d, open("/logs/verifier/reward.json", "w"))
PY
        rm -f /logs/verifier/verbatim_line_fraction.txt
    fi
    echo "REWARD correctness=${correct} speedups=[$*] geomean=${geomean} reward=${reward}"
    # Reopen to 0755 so a local docker run, which reads this from the host as
    # an ordinary user, can collect it. Still root-owned, so the candidate
    # still cannot write it.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    exit 0
}

# build_tree <srctree> <builddir> [as-candidate]
#
# The candidate's CMakeLists.txt executes arbitrary code during `cmake -S`, so
# that build runs as nobody; this step's own base is built in the image, as
# root, and never comes through here.
build_tree() {
    local src="$1" bld="$2" candidate="${3:-0}"
    local -a pre=()
    [ "$candidate" = "1" ] && pre=(run_unprivileged)
    mkdir -p "$bld"
    [ "$candidate" = "1" ] && chown -R "$SCORE_UID:$SCORE_GID" "$bld" 2>/dev/null || true
    "${pre[@]}" cmake -S "$src" -B "$bld" -DCMAKE_BUILD_TYPE=Release -DUVG_BUILD_TESTS=ON \
        > /tmp/cmake.log 2>&1 || { echo "configure failed"; tail -20 /tmp/cmake.log; return 1; }
    "${pre[@]}" make -C "$bld" -j"$(nproc)" > /tmp/build.log 2>&1 \
        || { echo "build failed"; tail -30 /tmp/build.log; return 1; }
    [ -x "$bld/bin/uvg266" ] || { echo "no uvg266 binary"; return 1; }
}

# build_harness <srctree> <builddir> <out> [as-candidate] -- compiled against
# that tree's own headers and static library, so every call goes through the
# strategy selector that tree installed.  A compile executes nothing from the
# tree it reads, but the binary it produces runs that tree's static
# initializers, so it is built by the same identity that will run it.
build_harness() {
    local src="$1" bld="$2" out="$3" candidate="${4:-0}"
    local -a pre=()
    [ "$candidate" = "1" ] && pre=(run_unprivileged)
    "${pre[@]}" gcc -O2 -o "$out" /tests/vendor/bench.c -I "$src" -I "$src/src" -I "$bld/src" -I "$bld" \
        "$bld/lib/libuvg266.a" -lm -lpthread \
        > /tmp/harness.log 2>&1 || { echo "harness build failed for $src"; tail -20 /tmp/harness.log; return 1; }
}

# Measurement pins to two cores so both sides see the same machine. The two
# harnesses are separate processes, so the no-op floor is small and positive
# rather than exactly zero; it is measured, not assumed.
PIN="taskset -c 0,1"
