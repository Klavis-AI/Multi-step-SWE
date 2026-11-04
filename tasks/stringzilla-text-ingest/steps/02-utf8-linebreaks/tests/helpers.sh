#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Keep in sync.
set -u

# Unprivileged identity for candidate code. nobody:nogroup already exists in
# debian:bookworm-slim, and it owns nothing the verifier reads back.
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
# CMakeLists is a program, a header is compiled into a harness, a linked
# library's static initializer runs before main -- not just a service.
run_unprivileged() {
    setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        env HOME=/tmp TMPDIR=/tmp "$@"
}

# Give /app to the unprivileged user. Call before an unprivileged build.
#
# /app arrives owned by whoever wrote the artifact. Without this a build that
# writes into the source tree cannot create its output directory, so every
# candidate scores 0 and the step looks merely very hard instead of broken.
give_app_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" /app 2>/dev/null || true
}

# run_unprivileged_probe <cmd> -- as run_unprivileged, and additionally capped
# to one process for that uid.
#
# Used wherever this tree's own library is exercised: the `dump` comparison and
# every timed invocation. The ladder's envelope says one thread, the reference
# is single-threaded, and both trees are pinned to the same two cores -- so a
# kernel that spawns threads or child processes must not be able to buy a third
# core the reference never had. RLIMIT_NPROC is lowered in the shell that
# execs the harness, so the cap covers the whole process lifetime and any
# thread it creates afterwards; `exec` is unaffected by the limit, so the
# harness still starts. Lowering the limit is best-effort: if it cannot be set
# the run still happens, and the honest single-threaded case is identical
# either way.
run_unprivileged_probe() {
    run_unprivileged bash -c 'ulimit -u 1 2>/dev/null || true; exec "$@"' bash "$@"
}

# The capability envelope this ladder is scored under, and the command line
# every translation unit is compiled with. Stated in the step instruction as a
# fairness rule and enforced by check_isa.py. The host lists SHA-NI (and this
# box has no AVX-512 at all), but the reference's kernels stop at AVX2 and the
# pinned command line below enables neither, so both are out of envelope: a
# candidate reaching for either would be measured against a machine the
# reference had no access to.
ENVELOPE_FLAGS=(-mavx2 -mbmi -mbmi2 -maes -mpclmul -msse4.2 -mlzcnt -mpopcnt)

# Cores the timings are pinned to. Both sides are pinned to the same pair, and
# the two sides are measured alternately, so a busy neighbour moves both.
PIN_CORES=0,1

# report <correctness 0|1> <speedup> [<speedup> ...]
#
#   reward = 0 if correctness != 1 or geomean <= 1; otherwise geomean is
#          scored against this step's oracle baseline B: 1 at or above B,
#          ln(geomean)/ln(B) in between, clamped to [0, 1].
#   geomean = exp( mean_f( ln(max(1, speedup_f)) ) )
#
# One speedup per function in this step's scope. The geometric mean is the right
# aggregator for ratios, and it penalises neglect: a function left alone
# contributes ln(1) = 0 and drags the step down, so a big win on one function
# cannot hide doing nothing on another.
#
# max(1, .) floors a regression at 0 rather than going negative. Correctness is
# binary and multiplies, so a tree that changes behaviour scores 0 however fast.
#
# The reward is normalized by this step's hardcoded oracle baseline and
# clamped to [0, 1].
ORACLE_BASELINE=8.9297000000
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
        # oracle binary step reward: 1 only above the hardcoded baseline.
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

# build_candidate <srctree> <builddir>
#
# Both halves run candidate code -- a CMakeLists is a program, and the build
# compiles and links whatever the candidate wrote -- so both go through
# run_unprivileged. The caller creates <builddir> for that uid first (test.sh).
build_candidate() {
    run_unprivileged cmake -S "$1" -B "$2" -DCMAKE_BUILD_TYPE=Release -DSTRINGZILLA_BUILD_TEST=ON \
        -DSTRINGZILLA_BUILD_BENCHMARK=OFF -DSTRINGZILLA_BUILD_SHARED=OFF \
        -DSTRINGZILLA_USE_SANITIZERS=OFF \
        -DCMAKE_C_FLAGS="${ENVELOPE_FLAGS[*]} -Wno-error=maybe-uninitialized -Wno-error=uninitialized" \
        -DCMAKE_CXX_FLAGS="${ENVELOPE_FLAGS[*]} -Wno-error=maybe-uninitialized -Wno-error=uninitialized" \
        > /tmp/cmake.log 2>&1 || { echo "candidate configure failed"; tail -20 /tmp/cmake.log; return 1; }
    run_unprivileged cmake --build "$2" --target stringzilla_test_cpp20 -j4 > /tmp/build.log 2>&1 \
        || { echo "candidate build failed"; tail -30 /tmp/build.log; return 1; }
}

# build_harness <srctree> <out>  -- compiled against that tree's own headers, so
# every public entry point resolves to the implementation that tree installs.
#
# The library is header-only, so this compile *is* candidate code: it runs the
# candidate's preprocessor, not just the compiler. Unprivileged, like the
# build. The base tree's harness is compiled in the verifier image, as root,
# and never passes through here.
build_harness() {
    run_unprivileged gcc -O2 -Wno-unknown-pragmas "${ENVELOPE_FLAGS[@]}" -o "$2" /tests/vendor/bench_sz.c -I "$1/include" \
        > /tmp/harness.log 2>&1 || { echo "harness build failed for $1"; tail -20 /tmp/harness.log; return 1; }
}

# time_one <harness> <fnid>
#
# A single-tree probe, so its harness is the tree under test and it runs
# unprivileged and process-capped. test.sh does not call it (it uses the
# alternating pair below); it is kept for one-sided diagnosis.
#
# One invocation is a median of seven repetitions over seven fresh draws of the
# input, plus the digest of all seven draws' outputs. Five invocations are
# taken, alternating between the two trees, and the median of the five medians
# is reported: a single whole-invocation shift --
# another job on the machine for the length of one run -- moves one sample and
# not the median. The internal median is what keeps a spuriously *fast* sample
# from becoming a spuriously fast speedup, so best-of-N is deliberately not used
# here.
ROUNDS=5
time_one() {
    local harness="$1" fnid="$2" round samples=() value
    for round in $(seq "$ROUNDS"); do
        value=$(run_unprivileged_probe taskset -c "$PIN_CORES" "$harness" "$fnid" bench 2>/dev/null | awk '{print $2}')
        [ -z "${value:-}" ] && return 1
        samples+=("$value")
    done
    printf '%s\n' "${samples[@]}" | sort -n | awk -v n="$ROUNDS" 'NR == int((n+1)/2)'
}

# time_pair <base-harness> <cand-harness> <fnid>
# Alternates the two trees round by round so that both see the same machine.
# Prints "<base> <cand> <spread-base%> <spread-cand%>".
#
# The third field the harness prints is a digest of the observable output of
# all seven timed draws.  Both trees are fed the same draws, so a tree that
# behaves like its base prints the base's digest, and one that answers a draw
# from a cached answer prints a different one.  Status 0 when the trees agree;
# 1 when a side produced no timing; 2 when the digests differ -- the caller
# turns that into a correctness failure, because the timing path checks output
# too, not only the single `dump` draw.
time_pair() {
    local hb="$1" hc="$2" fnid="$3" round tb tc b=() c=() lb lc db="" dc=""
    for round in $(seq "$ROUNDS"); do
        # $hb is the base harness built in this verifier image -- verifier-owned
        # code -- so it stays root. $hc is compiled from the candidate's headers
        # and runs candidate code, so it goes through the unprivileged,
        # process-capped wrapper.
        lb=$(taskset -c "$PIN_CORES" "$hb" "$fnid" bench 2>/dev/null) || lb=""
        lc=$(run_unprivileged_probe taskset -c "$PIN_CORES" "$hc" "$fnid" bench 2>/dev/null) || lc=""
        tb=$(printf '%s\n' "$lb" | awk 'NR==1{print $2}')
        tc=$(printf '%s\n' "$lc" | awk 'NR==1{print $2}')
        [ -n "$db" ] || db=$(printf '%s\n' "$lb" | awk 'NR==1{print $3}')
        [ -n "$dc" ] || dc=$(printf '%s\n' "$lc" | awk 'NR==1{print $3}')
        [ -z "${tb:-}" ] || b+=("$tb")
        [ -z "${tc:-}" ] || c+=("$tc")
    done
    [ "${#b[@]}" -lt 3 ] || [ "${#c[@]}" -lt 3 ] && return 1
    local mb mc sb sc
    mb=$(printf '%s\n' "${b[@]}" | sort -n | awk -v n="${#b[@]}" 'NR == int((n+1)/2)')
    mc=$(printf '%s\n' "${c[@]}" | sort -n | awk -v n="${#c[@]}" 'NR == int((n+1)/2)')
    sb=$(printf '%s\n' "${b[@]}" | sort -n | awk -v n="${#b[@]}" 'NR==1{lo=$1} NR==n{hi=$1; printf "%.1f", 100*(hi-lo)/lo}')
    sc=$(printf '%s\n' "${c[@]}" | sort -n | awk -v n="${#c[@]}" 'NR==1{lo=$1} NR==n{hi=$1; printf "%.1f", 100*(hi-lo)/lo}')
    echo "$mb $mc $sb $sc"
    if [ -z "$db" ] || [ -z "$dc" ] || [ "$db" != "$dc" ]; then
        # To stderr: this function's stdout is the ratio line its caller reads.
        echo "  $fnid: the seven timed draws do not reproduce this step's output (base digest ${db:-none}, candidate digest ${dc:-none})" >&2
        return 2
    fi
    return 0
}

# time_guard <base-harness> <cand-harness> <fnid>
#
# The end-to-end guard is pass/fail, so it is not a ratio estimate and must not
# be defeated by a neighbour's load: each side is timed GUARD_ROUNDS times, each
# round a median of seven repetitions, and the two sides are compared by their
# *best* round.  Load can only ever make a run slower, so the minimum is the
# cleanest estimate of what each side costs when the machine is free, and the
# threshold below is then a statement about the pipeline rather than about the
# host.  Measured on this box: two byte-identical trees compared by medians
# differed by 27% while a neighbour was building; compared by minima they agree
# to within a few percent.
GUARD_ROUNDS=7
time_guard() {
    local hb="$1" hc="$2" fnid="$3" round b=() c=() value
    for round in $(seq "$GUARD_ROUNDS"); do
        # Same split as time_pair: base root, candidate unprivileged + capped.
        value=$(taskset -c "$PIN_CORES" "$hb" "$fnid" bench 2>/dev/null | awk '{print $2}')
        [ -n "${value:-}" ] && b+=("$value")
        value=$(run_unprivileged_probe taskset -c "$PIN_CORES" "$hc" "$fnid" bench 2>/dev/null | awk '{print $2}')
        [ -n "${value:-}" ] && c+=("$value")
    done
    [ "${#b[@]}" -eq 0 ] || [ "${#c[@]}" -eq 0 ] && return 1
    printf '%s %s\n' "$(printf '%s\n' "${b[@]}" | sort -n | head -1)" \
                      "$(printf '%s\n' "${c[@]}" | sort -n | head -1)"
}

# measure_functions <base-harness> <cand-harness> <funcs-file> <speedups-name> <correct-name>
measure_functions() {
    local base_harness="$1" cand_harness="$2" funcs="$3"
    local -n _speedups="$4"
    local -n _correct="$5"
    local fnid tb tc sp ok line sb sc rc
    while read -r fnid; do
        [ -z "$fnid" ] && continue
        case "$fnid" in \#*) continue ;; esac
        # The base dump is verifier-owned code and stays root. The candidate
        # dump is compiled from the candidate's own headers, so it runs
        # unprivileged and capped, exactly as the timed invocations do.
        if ! "$base_harness" "$fnid" dump > /tmp/d_base.bin 2>/tmp/d_base.err; then
            echo "  $fnid: base dump failed"; _correct=0; continue
        fi
        if ! run_unprivileged_probe "$cand_harness" "$fnid" dump > /tmp/d_cand.bin 2>/tmp/d_cand.err; then
            echo "  $fnid: candidate dump failed"; _correct=0; continue
        fi
        # An empty dump on either side means the call did not produce the
        # output it is supposed to, which compares equal to nothing at all.
        # "no output" must not read as agreement.
        if [ ! -s /tmp/d_base.bin ] || [ ! -s /tmp/d_cand.bin ]; then
            echo "  $fnid: empty dump (base $(wc -c < /tmp/d_base.bin) B, candidate $(wc -c < /tmp/d_cand.bin) B)"
            _correct=0; continue
        fi
        if cmp -s /tmp/d_base.bin /tmp/d_cand.bin; then ok="exact"; else ok="MISMATCH"; _correct=0; fi
        line=$(time_pair "$base_harness" "$cand_harness" "$fnid"); rc=$?
        if [ "$rc" = 2 ]; then
            # The timed draws' outputs are already reported above; the ratio is
            # printed beside them for diagnosis, but the step cannot be correct.
            _correct=0
        elif [ "$rc" != 0 ] || [ -z "$line" ]; then
            echo "  $fnid: no timing"; _correct=0; continue
        fi
        read -r tb tc sb sc <<< "$line"
        if [ "${tb:-0}" = "0" ] || [ "${tc:-0}" = "0" ]; then
            echo "  $fnid: zero timing"; _correct=0; continue
        fi
        sp=$(awk -v a="$tb" -v b="$tc" 'BEGIN{ if (b+0<=0) print 0; else printf "%.4f", a/b }')
        printf "  %-24s %10s -> %10s us   %8sx   %-8s  spread %s%%/%s%%\n" \
            "$fnid" "$tb" "$tc" "$sp" "$ok" "$sb" "$sc"
        _speedups+=("$sp")
    done < "$funcs"
}
