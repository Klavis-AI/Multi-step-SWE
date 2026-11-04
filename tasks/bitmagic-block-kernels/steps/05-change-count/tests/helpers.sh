#!/usr/bin/env bash
# Shared verifier helpers for one step of bitmagic-block-kernels.
#
# The verifier image is built with that step's tests/ as the Docker build
# context, so this file cannot COPY from the task root. It is byte-identical for
# every step -- a substitution there is a no-op -- and every per-step value
# lives in a file whose NAME carries the step number (FUNCS.stepNN).
set -u

# Unprivileged identity for candidate code. nobody:nogroup already exists in
# debian:bookworm-slim.
SCORE_UID=65534
SCORE_GID=65534

# Lock the reward directory to root. Call this first in test.sh.
#
# The session runtime chmods /logs/verifier to 777 just before this container
# starts, so anything running here can overwrite reward.json -- including
# candidate code the checks build or run. Writing the real reward last does not
# help: a detached child outlives the reporting process and writes after it.
#
# This runs after that chmod, so this is the mode that sticks.
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
# candidate source -- not just a service.  Here that is: the harness build, the
# end-to-end driver build, the project's own cmake configure, and every binary
# linked against the candidate's headers, whose static initializers run before
# main().
run_unprivileged() {
    setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        env HOME=/tmp TMPDIR=/tmp "$@"
}

# Give /app to the unprivileged user. Call before an unprivileged build.
#
# /app arrives owned by whoever wrote the artifact. Without this the build
# cannot create its output directory, so every candidate gets 0 and the step
# looks merely very hard instead of broken.
give_app_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" /app 2>/dev/null || true
}

# report <correctness 0|1> <speedup> [<speedup> ...]
#
# Writes /logs/verifier/reward.json. The step reward is binary and
# correctness-gated: 1 only when correctness is 1 and the geometric mean of the
# per-entry speedups is above ORACLE_BASELINE, otherwise 0.
#
# One speedup per in-scope entry point.  The geometric mean is the right
# aggregator for ratios and it penalises neglect: an entry left alone
# contributes ln(1) = 0 and drags the step down, so a large win on one entry
# cannot hide doing nothing on another.  max(1, .) floors a regression at 0
# rather than going negative; correctness is binary and gates the reward, so a
# tree that changes behaviour is rejected however fast it is.
ORACLE_BASELINE=5.0032000000
report() {
    local correct="${1:-0}"; shift || true
    local reward
    local geo; geo=$(awk 'BEGIN {
        n = ARGC - 1; if (n <= 0) { printf "%.4f", 1; exit }
        s = 0
        for (i = 1; i <= n; i++) { v = ARGV[i] + 0; if (v < 1) v = 1; s += log(v) }
        printf "%.4f", exp(s / n)
    }' "$@")
    mkdir -p /logs/verifier
    # Binary step reward: 1 only above the baseline.
    if [[ "${correct:-0}" != "1" ]] || ! awk -v g="${geo:-0}" -v b="$ORACLE_BASELINE" 'BEGIN { exit !(g > b) }'; then
        reward="0.000000"
    else
        reward="1.000000"
    fi
printf '{"reward":%s,"correctness":%s,"geomean_speedup":%s}\n' \
        "$reward" "${correct:-0}" "$geo" > /logs/verifier/reward.json
    # Reopen to 0755 so a local docker run, which reads this from the host as
    # an ordinary user, can collect it. Still root-owned, so the candidate
    # still cannot write it.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    echo "REWARD correctness=${correct} speedups=[$*] geomean=${geo} reward=${reward}"
    exit 0
}

# The capability envelope, identical for both sides.
# A preprocessor macro selects BitMagic's backend and the compiler flags must
# enable the corresponding instructions -- docs/build.md says so explicitly, and
# upstream's own BMAVX2OPT preset is `-march=skylake -mavx2 -O2`.
ENVELOPE="-std=c++17 -O2 -DNDEBUG -march=skylake -mavx2 -mpopcnt -DBMAVX2OPT"

# build_harness <src-tree> <harness.cpp> <out>   -- compiled against that tree's
# own headers, so it calls whatever that tree installs for each entry point.
#
# The compiler itself runs unprivileged: it is reading a candidate-supplied
# header set, and the binary it writes is the candidate's code.  Only the
# candidate tree is ever passed here -- the base harness is built inside the
# verifier image, as root, by tests/Dockerfile.
build_harness() {
    run_unprivileged g++ $ENVELOPE -I "$1/src" -o "$3" "$2" > /tmp/harness_build.log 2>&1 \
        || { echo "  harness build failed for $1"; tail -25 /tmp/harness_build.log | sed 's/^/    /'; return 1; }
}

# build_e2e <src-tree> <driver.cpp> <out>
build_e2e() {
    run_unprivileged g++ $ENVELOPE -I "$1/src" -o "$3" "$2" > /tmp/e2e_build.log 2>&1 \
        || { echo "  end-to-end driver build failed for $1"; tail -25 /tmp/e2e_build.log | sed 's/^/    /'; return 1; }
}

# configure_project <src-tree> <build-dir>
#
# The project's own build system must still configure -- it lists every backend
# header in BM_HEADERS, so a header a candidate removed or renamed fails here.
# The acceptance suite itself (tests/stress/t.cpp) is NOT run: every selectable
# group (-ll, -bvb, -bvo, -bvs, -bvser) exceeds a 240 s cap without completing,
# and the full run spends more than half an hour inside
# a single search-family test -- Test bit_find_first_diff(), which depends on the
# `search` kernels this ladder strips at every level.  A capped run therefore
# never reaches the block tests it is meant to guard, and a check that examines
# nothing is worse than no check.  The collateral guard is the composite
# end-to-end driver below instead, plus the per-entry byte-identical comparison.
configure_project() {
    local n
    # The build directory is this script's own scratch space, so hand it to the
    # unprivileged user before the configure runs there.  cmake then runs
    # unprivileged: it interprets the candidate's CMakeLists.txt, which is
    # candidate code, and a configure that ran as root could rewrite the reward.
    mkdir -p "$2"
    chown -R "$SCORE_UID:$SCORE_GID" "$2" 2>/dev/null || true
    run_unprivileged cmake -S "$1" -B "$2" -DCMAKE_BUILD_TYPE=Release -DBMOPTFLAGS=BMAVX2OPT > /tmp/cmake.log 2>&1 \
        || { echo "  the project's own CMake build no longer configures"; tail -20 /tmp/cmake.log | sed 's/^/    /'; return 1; }
    n=$(ls "$2/CMakeFiles" 2>/dev/null | wc -l)
    echo "  cmake configure OK ($n CMakeFiles entries under $(basename "$2"))"
    [ "$n" -gt 0 ] || { echo "  the configure produced no build files"; return 1; }
    return 0
}

# envelope_check <candidate-src-tree>
#
# The capability envelope: this ladder targets AVX2 -- the reference
# implementation is upstream's AVX2 backend (256-bit) and the target host has
# no AVX-512.  check_isa.sh enforces it against the C/C++ lines a candidate
# ADDED.
#
# Scope.  A milestone owns the files its own reference patch touches
# (/tests/SCOPE.stepNN, one path per line, generated from solution/oracle.patch
# and checked against the ladder's level-to-level diff).  The gate's decision is
# scoped to those files and states what it examined:
#
#   * no scoped file differs from the base, and the candidate adds or modifies
#     nothing anywhere else either -- the tree is the one it started from, so
#     there is no added code the envelope could be violated by.  The check is
#     skipped and says so, with the count: skipping is a statement about the
#     tree, not a clean bill of health.  Correctness is not touched -- behaviour
#     is unchanged because nothing was changed, which is the question
#     correctness asks, and a tree that implemented nothing is caught by the
#     speedup half (all ratios 1.0, ln(1) = 0), never here.
#   * otherwise -- the candidate wrote code, so the added-line check runs exactly
#     as it always has, over the whole added line set (writing AVX-512 is out of
#     envelope at every milestone, not only in the file where it is written).
#
# This never weakens the check.  "Adds or modifies nothing" is measured with
# `diff -rq`, ignoring entries that exist only in the BASE (a workspace from
# which a directory was dropped, e.g. by an artifact filter): a file the
# candidate does not have cannot carry a line the candidate added.  Everything
# the old unconditional call could have found a violation in -- any file the
# candidate created, changed or kept with different content -- still runs the
# checker, and check_isa.sh itself is untouched.
#
# A gate must assert it examined something, so the scope
# list and the candidate's diff are both counted; a missing or empty scope file
# fails closed rather than passing silently.
envelope_check() {
    local src="$1" __scopefile __f nscoped=0 nchanged=0 ncand
    __scopefile=$(ls -1 /tests/SCOPE.step* 2>/dev/null)
    if [ -z "$__scopefile" ]; then
        echo "  envelope: no /tests/SCOPE.step* scope file -- cannot decide; FAILING CLOSED"
        return 1
    fi
    while read -r __f; do
        case "$__f" in ''|'#'*) continue;; esac
        nscoped=$((nscoped + 1))
        if [ ! -f "$src/$__f" ] || ! cmp -s "/base/$__f" "$src/$__f"; then
            nchanged=$((nchanged + 1))
        fi
    done < "$__scopefile"
    if [ "$nscoped" -eq 0 ]; then
        echo "  envelope: $(basename "$__scopefile") names no file -- cannot decide; FAILING CLOSED"
        return 1
    fi
    # Candidate-side differences: files it modified, and entries it has that the
    # base does not.  "Only in /base..." lines are dropped -- see the note above.
    ncand=$(diff -rq /base "$src" 2>/dev/null | grep -Ev '^Only in /base[:/]' | grep -c . || true)
    if [ "$nchanged" -eq 0 ] && [ "$ncand" -eq 0 ]; then
        echo "  envelope: $nscoped of $nscoped scoped file(s) unchanged by the candidate -- check skipped"
        echo "    (scope from $(basename "$__scopefile"): $(grep -v '^#' "$__scopefile" | tr '\n' ' '))"
        echo "    (the candidate adds or modifies nothing: 0 candidate-side difference(s) vs the base)"
        return 0
    fi
    if [ "$nchanged" -gt 0 ]; then
        echo "  envelope: $nchanged of $nscoped scoped file(s) changed by the candidate -- checking added lines"
    else
        echo "  envelope: 0 of $nscoped scoped file(s) changed, but the candidate adds or modifies files elsewhere -- checking added lines"
    fi
    bash /tests/check_isa.sh /base "$src"
}
