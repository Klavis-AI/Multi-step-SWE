#!/usr/bin/env bash
# Per-step copy: a verifier image is built with that step's tests/ as the
# Docker build context, so the file cannot be referenced from the task root.
# Identical in every step.
set -u

# ---------------------------------------------------------------------------
# Reward hardening.  Harbor chmods /logs/verifier to 777 as root just before
# this container starts, so anything running in here can overwrite
# reward.json -- including candidate code the verifier builds or runs.  Writing
# the real score last does not help: a detached child outlives the scorer and
# writes after it.  A candidate that implements nothing would then score 1.0.
#
# Two helpers close this and both are needed: harden_reward_dir takes the
# directory back to root:root 0700 -- it runs after Harbor's chmod, so it wins
# -- and run_unprivileged runs candidate code as nobody, because root can write
# a root-owned directory whatever its mode.  report() reopens the directory on
# the way out.
# ---------------------------------------------------------------------------

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

# run_as <runner> <command> [args...]
#
#   Run <command> through <runner> when one is named, directly when it is "".
#   The runner is a shell function (`run_unprivileged`), so it cannot be the
#   head word of a command list run under `timeout` or `taskset`; this is the
#   one place that ordering is written down.  "" means the command is
#   verifier-owned and runs as root.
run_as() {
    local runner="$1"; shift
    if [ -n "$runner" ]; then
        "$runner" "$@"
    else
        "$@"
    fi
}

# report <correctness 0|1> <speedup> [<speedup> ...]
#
#   reward = 0 if correctness != 1 or value <= 1, where value is the
#   geometric mean of max(1, speedup_f) over the functions.  Otherwise value
#   is scored against this step's oracle baseline B: 1 at or above B,
#   ln(value)/ln(B) in between, clamped to [0, 1].
#
# One speedup per function in this step's scope.  The geometric mean is the
# right aggregator for ratios and it penalises neglect: a function left alone
# contributes ln(1) = 0 and drags the step down, so a large win on one kernel
# cannot hide doing nothing on another.
#
# max(1, .) floors a regression at 0 rather than going negative.  Correctness
# is a binary gate: a tree that changes behaviour scores 0 however fast it is.
# Reopen the reward directory for collection.  report() calls this on the way
# out, so a local docker run -- which reads the reward from the host as an
# ordinary user, where 0700 reads as a *missing* reward -- still gets it.  The
# directory and the file stay root-owned, so the candidate still cannot write
# them.
reopen_reward_dir() {
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
}

ORACLE_BASELINE=3.4123000000
report() {
    local correct="${1:-0}"; shift || true
    local reward geo
    if [ "$#" -eq 0 ]; then
        echo "no functions measured"
        mkdir -p /logs/verifier
            # oracle binary step reward: 1 only above the hardcoded baseline.
    if [[ "${correct:-0}" != "1" ]] || ! awk -v g="${geo:-0}" -v b="$ORACLE_BASELINE" 'BEGIN { exit !(g > b) }'; then
        reward="0.000000"
    else
        reward="1.000000"
    fi
printf '{"reward":0,"correctness":0,"functions_measured":0,"geomean_speedup":1}\n' \
            > /logs/verifier/reward.json
        reopen_reward_dir
        exit 0
    fi
    reward=$(awk -v c="$correct" -v B="$ORACLE_BASELINE" 'BEGIN {
        c += 0; if (c != 1) { printf "%.6f", 0; exit }
        n = ARGC - 1
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
    geo=$(awk 'BEGIN {
        n = ARGC - 1
        s = 0
        for (i = 1; i <= n; i++) { v = ARGV[i] + 0; if (v < 1) v = 1; s += log(v) }
        printf "%.4f", exp(s / n)
    }' "$@")
    mkdir -p /logs/verifier
    printf '{"reward":%s,"correctness":%s,"functions_measured":%s,"geomean_speedup":%s}\n' \
        "$reward" "${correct:-0}" "$#" "$geo" > /logs/verifier/reward.json
    echo "REWARD correctness=${correct} functions=$# speedups=[$*] geomean=${geo} reward=${reward}"
    reopen_reward_dir
    exit 0
}

# build_tree <srctree> <builddir> [runner]
#   meson+ninja is the build the verifier uses.  `-Dbuildtype=release` is what
#   the agent's tree is graded with, and it is the same for every tree.
#
#   [runner] is `run_unprivileged` for the candidate's tree: meson reads the
#   candidate's own build descriptions and ninja runs its compilers on the
#   candidate's sources, so this build IS candidate code running -- a
#   `meson.build` can do anything a shell command can, and as root it could
#   overwrite /logs/verifier/reward.json.  "" for a verifier-owned tree.
#
#   The build directory is not pre-created: it has to be created by the user
#   that will fill it, and /tmp is world-writable, so meson can do that itself.
build_tree() {
    local srctree="$1" builddir="$2" runner="${3:-}"
    rm -rf "$builddir"
    run_as "$runner" timeout 300 meson setup "$builddir" "$srctree" --buildtype=release > /tmp/meson.log 2>&1 \
        || { echo "  meson setup failed for $srctree"; tail -20 /tmp/meson.log; return 1; }
    run_as "$runner" timeout 2400 ninja -C "$builddir" -j"$(nproc)" > /tmp/ninja.log 2>&1 \
        || { echo "  build failed for $srctree"; grep -E "error:|undefined reference" /tmp/ninja.log | head -20; return 1; }
    return 0
}

# suite_failures <builddir> <srcdir> <logprefix> [runner] -> failing test programs
#   The project declares its own suite as TESTS = test_isa, test_basic,
#   test_verify.test (Makefile.am) / ADD_TEST(test_isa, test_basic, test_verify)
#   (CMakeLists.txt); the meson build produces the binaries it lists in
#   tests/meson.build and registers no test() calls, so they are run directly
#   here, the same way `make check` does.  (That list has test_isa and
#   test_verify but not test_basic, so test_basic is reported as not built on
#   either side and the base-relative comparison below sees no difference.)
#   A declared suite binary that the tree does not build is itself a failure,
#   recorded as one: skipping it (the `[ -x ] || continue` this replaces) let a
#   binary the base writes and the candidate dropped read as "no new failures",
#   and for test_verify left the scoped report below reading clean off a
#   missing log.  Recording it on both sides is what keeps the comparison
#   honest -- only a binary the BASE built and the candidate did not shows up
#   as new.
#
#   [runner] is passed to run_as for the test programs themselves:
#   `run_unprivileged` for the candidate's tree, because those binaries are
#   candidate code and run in this container (a static initializer alone is
#   enough to write a world-writable reward file); "" for the verifier's base
#   build, which is built from pinned sources the candidate cannot touch.
suite_failures() {
    local b="$1" d="$2" lp="$3" runner="${4:-}" t
    : > /tmp/suite_fail.txt
    for t in test_isa test_basic; do
        if [ ! -x "$b/tests/$t" ]; then
            echo "$t (not built)" >> /tmp/suite_fail.txt
            continue
        fi
        run_as "$runner" timeout 600 "$b/tests/$t" \
            > "/tmp/${lp}_$t.log" 2>&1 || echo "$t" >> /tmp/suite_fail.txt
    done
    if [ -x "$b/tests/test_verify" ]; then
        run_as "$runner" timeout 1200 "$b/tests/test_verify" -f "$d/data/test_small_2.fasta" \
            > "/tmp/${lp}_test_verify.log" 2>&1 || echo "test_verify" >> /tmp/suite_fail.txt
    else
        echo "test_verify (not built)" >> /tmp/suite_fail.txt
    fi
    sort -u /tmp/suite_fail.txt
}

# scoped_verify_errors <logfile> <funcs-file>
#   Count the lines in an upstream test_verify log that report a wrong result
#   for an entry point in THIS step's scope.  Upstream's test_verify reports
#   inconsistencies on stderr and still exits 0, so its exit status carries
#   almost no information; the per-function report it prints does.  Restricted
#   to the scope on purpose: the stripped tree's own pruned tables produce none
#   for scoped names (they route to the reference), and this step's families
#   are exactly what must not appear there.  Two upstream families (the local
#   and semi-global diagonal recurrences) disagree with upstream's own
#   reference on some inputs and are excluded from every scope for that reason,
#   so they are not counted here either.
scoped_verify_errors() {
    python3 -I - "$1" "$2" <<'PYEOF'
import re, sys
log, funcs = sys.argv[1], sys.argv[2]
scope = {l.strip() for l in open(funcs) if l.strip()}
isa = re.compile(r"_(sse2|sse41|avx2|altivec|neon)_\d+_(\d+|sat)$")
n = 0
seen = set()
for line in open(log, errors="replace"):
    if "wrong" not in line and "!=" not in line:
        continue
    m = re.match(r"parasail_([A-Za-z0-9_]+)\(", line)
    if not m:
        continue
    name = isa.sub("", m.group(1))
    if name in scope:
        n += 1
        seen.add(name)
for name in sorted(seen):
    print("    %s reports a wrong result" % name)
print("  scoped entry points reported wrong: %d" % len(seen))
sys.exit(0)
PYEOF
}
