#!/usr/bin/env bash
# Shared verifier helpers. Per-step copy: the verifier image is built with this
# step's tests/ as the Docker build context, so it cannot COPY from the task
# root. Keep byte-identical across steps.
set -u

# report <correctness 0|1> <speedup> [isa_objects_examined]
#
#   reward = correctness x ln(max(1, speedup))
#
# where `speedup` is the GEOMETRIC MEAN of this step's per-function speedups, so
# the reward is `correctness x mean_f( ln(max(1, speedup_f)) )`. Correctness is
# binary and multiplies: a tree that fails its own per-function comparison, or
# breaks something elsewhere, scores 0 however fast it is.
#
# The logarithm is what makes the aggregate mean something. Speedups compose
# multiplicatively, so their logs compose additively; within a step the mean of
# logs over functions is the log of their geometric mean, and across steps
# multi_step_reward_strategy = "mean" makes the trial reward the log of the
# geometric mean over every function in the task. A function left untouched
# contributes ln(1) = 0 and drags its step down.
#
# Every value written to reward.json is a number: the runner parses each key under
# `rewards` as a float, and one string field aborted a nine-step trial after
# step 1 had graded perfectly.
#
# This is deliberately NOT the standard template's clamped report(): the reward
# here is not confined to [0, 1].
report() {
    local correct="${1:-0}" speedup="${2:-0}" isa="${3:-0}" reward
    reward=$(awk -v c="$correct" -v s="$speedup" 'BEGIN {
        c += 0; s += 0
        if (c != 1) c = 0
        if (s < 1) s = 1
        printf "%.6f", c * log(s)
    }')
    mkdir -p /logs/verifier
        # P75 binary step reward: 1 only above the hardcoded baseline.
    if [[ "${correct:-0}" != "1" ]] || ! awk -v g="${speedup:-0}" -v b="$P75_BASELINE" 'BEGIN { exit !(g > b) }'; then
        reward="0.000000"
    else
        reward="1.000000"
    fi
printf '{"reward":%s,"correctness":%s,"speedup":%s,"isa_objects":%s}\n' \
        "$reward" "${correct:-0}" "${speedup:-0}" "${isa:-0}" > /logs/verifier/reward.json
    python3 - "$reward" "${correct:-0}" "${speedup:-0}" "${isa:-0}" <<'PY'
import json, sys
r = {"reward": float(sys.argv[1]), "correctness": float(sys.argv[2]),
     "speedup": float(sys.argv[3]), "isa_objects": float(sys.argv[4])}
assert all(isinstance(v, float) for v in r.values())
print("REWARD " + json.dumps(r))
PY
    exit 0
}

# Cap build parallelism: nproc reports the host's core count inside a container
# with only a CPU quota.
build_jobs() {
    local j; j=$(nproc 2>/dev/null || echo 4)
    [ "$j" -gt 8 ] && j=8
    echo "$j"
}

CONFIGURE_FLAGS=(
    --disable-everything --enable-swscale --enable-checkasm
    --disable-doc --disable-programs --disable-network --disable-autodetect
    --disable-inline-asm --disable-vulkan --disable-cuda --disable-cuda-llvm
    --disable-ffnvcodec --disable-nvdec --disable-nvenc --disable-vaapi
    --disable-vdpau --disable-libdrm
)

# build_tree <srcdir> <builddir>
#
# One fixed configuration for every level, so the only thing that varies
# between the base and a candidate's tree is which optimized kernels are
# present. HAVE_INLINE_ASM is 0 and HAVE_X86ASM is 1 in every level.
build_tree() {
    local src="$1" bld="$2" tag
    tag=$(basename "$bld")
    mkdir -p "$bld"
    if [ ! -f "$bld/config.mak" ]; then
        ( cd "$bld" && "$src/configure" "${CONFIGURE_FLAGS[@]}" ) \
            > /tmp/configure-$tag.log 2>&1 \
            || { echo "  configure failed"; tail -20 /tmp/configure-$tag.log; return 1; }
    fi
    make -C "$bld" -j"$(build_jobs)" > /tmp/make-$tag.log 2>&1 \
        || { echo "  build failed"; tail -30 /tmp/make-$tag.log; return 1; }
    make -C "$bld" -j"$(build_jobs)" checkasm >> /tmp/make-$tag.log 2>&1 \
        || { echo "  checkasm build failed"; tail -30 /tmp/make-$tag.log; return 1; }
    [ -x "$bld/tests/checkasm/checkasm" ] \
        || { echo "  no checkasm binary was produced"; return 1; }
}

# bench_rows <checkasm stdout> -> number of rows in its benchmark table.
bench_rows() {
    awk '/^Benchmark results/{f=1;next} f && /^  [A-Za-z0-9_]/ {n++} END{print n+0}' "$1"
}

# run_checkasm <builddir> <outfile> <seed> [args...]
#
# checkasm's benchmark estimator carries a bare assert (`count > 0` in
# tests/checkasm/ext/src/stats.c:119) on a condition that is not guaranteed:
# when the bench loop records no samples it aborts with SIGABRT, exit 134, and
# prints a benchmark table with no rows at all. Measured on this host at
# roughly one run in four under load, and not seed-specific -- the same seed
# aborts and then completes.
#
# It zeroes a byte-correct tree: the scored differential suite passes, but the
# table the score is read from is empty, so the milestone reads as a failure.
# Nothing in the candidate's tree causes it and nothing in the tree can fix it,
# so the remedy is to run again. `timeout` is here for the same reason: a
# module that stalls under load must not consume the verifier's whole budget.
run_checkasm() {
    local bld="$1" out="$2" seed="$3"; shift 3
    timeout "${CHECKASM_TIMEOUT:-600}" \
        "$bld/tests/checkasm/checkasm" "$@" "$seed" > "$out" 2> "$out.err"
    return $?
}

# checkasm_attempt <builddir> <outfile> <seed> <args...> -> 0 if usable
#
# A run is usable when it neither crashed nor stalled and produced a benchmark
# table with rows. A run that legitimately failed a comparison is NOT retried:
# it exits non-zero but is reported, and the failures are what the caller wants.
checkasm_attempt() {
    local bld="$1" out="$2" seed="$3"; shift 3
    local rc=0 rows
    run_checkasm "$bld" "$out" "$seed" "$@" || rc=$?
    rows=$(bench_rows "$out")
    if [ "$rc" -eq 0 ] && [ "$rows" -gt 0 ]; then
        return 0
    fi
    if grep -qE 'FAILURE:|\[FAILED\]' "$out.err" "$out" 2>/dev/null; then
        return 1                      # a real failure; not a crash
    fi
    echo "  checkasm attempt seed $seed produced rc=$rc rows=$rows -- retrying"
    return 2
}

# checkasm_retry <builddir> <outfile> <seed> <args...> -> 0 if a usable run
# happened, 1 if every attempt was a crash or a stall.
#
# The retries are bounded twice over: at most three attempts, and at most
# CHECKASM_BUDGET seconds of wall clock in total for this call site. A whole
# verifier run makes six of these, so an unbounded retry loop on a host that is
# merely *slow* -- which is what was observed: one benchmark took twelve minutes
# of CPU against the thirty-five seconds it needs on a quiet machine -- would
# eat the verifier's whole timeout and turn a correct tree into no reward at
# all. Bounding it turns that into a named failure instead.
checkasm_retry() {
    local bld="$1" out="$2" seed="$3"; shift 3
    local attempt rc start
    start=$(date +%s)
    for attempt in 0 1 2; do
        rc=0
        checkasm_attempt "$bld" "$out" "$((seed + attempt * 101))" "$@" || rc=$?
        [ "$rc" -eq 0 ] && return 0
        [ "$rc" -eq 1 ] && return 1     # real failures; retrying changes nothing
        if [ $(( $(date +%s) - start )) -ge "${CHECKASM_BUDGET:-600}" ]; then
            echo "  checkasm retry budget exhausted for $out"
            break
        fi
    done
    return 1
}

# A harness that can pass while measuring nothing is worse than no harness, so
# prove it is live before trusting it. The probe is range conversion, which is
# the one part of libswscale this ladder never strips at any level, so it always
# has both a C reference and an optimized implementation to compare. Probing a
# stripped stage would be wrong: on a tree where the stage is absent there is
# correctly nothing to compare, which is exactly what the base looks like.
assert_checkasm_live() {
    local bld="$1" out=/tmp/live.log
    checkasm_retry "$bld" "$out" 1 --bench --test='sw_range_convert' || true
    if ! grep -q "Benchmark results" "$out" 2>/dev/null; then
        echo "  checkasm produced no benchmark results. Its output was:"
        sed 's/^/    /' "$out" 2>/dev/null | head -20
        return 1
    fi
    if ! grep -qE "\(\s*[0-9.]+x\)" "$out"; then
        echo "  checkasm benchmarked nothing against its C reference:"
        sed 's/^/    /' "$out" | head -20
        return 1
    fi
    return 0
}

# build_harness <srcdir> <builddir> <output>
#
# The end-to-end guard's measuring program, compiled here from the verifier's
# own copy of measure_sws.c and linked against the tree under test, so the
# instrument is never candidate-supplied.
build_harness() {
    local src="$1" bld="$2" out="$3"
    gcc -O2 -o "$out" /tests/vendor/measure_sws.c \
        -I"$bld" -I"$src" \
        "$bld/libswscale/libswscale.a" "$bld/libavutil/libavutil.a" \
        -lm -lpthread > /tmp/harness.log 2>&1 \
        || { echo "  measurement harness failed to link against this tree";
             tail -20 /tmp/harness.log; return 1; }
}

# checkasm_log <builddir> <outfile> [seed]
#
# One run over every libswscale checkasm module, baseline level first, then
# each CPU feature level the host has. `--test=sw_scale` selects nothing in this
# checkasm build; the wildcard is what selects exactly the eight modules.
# A failure anywhere suppresses the benchmark table for the whole run, so the
# log is parsed for both, and a crashed or stalled run is retried.
checkasm_log() {
    local bld="$1" out="$2" seed="${3:-1}"
    checkasm_retry "$bld" "$out" "$seed" --test='sw_*' --bench
}

# checkasm_failures <logfile> -> failing functions, one per line, with the CPU
# level suffix removed so the names line up with tests/FUNCS.
# e2e_dump <harness> <outdir> -> one raw plane dump per configuration.
e2e_dump() {
    local hn="$1" out="$2" cfg
    mkdir -p "$out"
    while read -r cfg; do
        [ -n "$cfg" ] || continue
        "$hn" "$cfg" dump "$out/$cfg.raw" > /dev/null 2>&1 \
            || { echo "  harness failed on config $cfg"; return 1; }
    done < /tests/E2E_CONFIGS
}

# e2e_time <harness> <reps> -> one "<config> <best_ms>" line per configuration,
# best over reps, interleaved so both harnesses see the same machine.
e2e_time() {
    local hn="$1" reps="$2" cfg r v best
    while read -r cfg; do
        [ -n "$cfg" ] || continue
        best=999999
        for r in $(seq "$reps"); do
            v=$("$hn" "$cfg" 1 2>/dev/null | awk '{print $3}')
            [ -n "$v" ] || v=999999
            best=$(awk -v x="$best" -v y="$v" 'BEGIN{print (y<x)?y:x}')
        done
        echo "$cfg $best"
    done < /tests/E2E_CONFIGS
}

# checkasm_failures <logfile> -> failing functions, one per line, with the CPU
# level suffix removed so the names line up with tests/FUNCS.
#
# checkasm prints both a per-function `FAILURE: <label>_<suffix> (file:line)`
# line and a grouped `- <test>.<group> [FAILED]` line. The per-function form is
# what the scope intersection needs, so it is what is parsed; the grouped form
# is folded in too, since a crash inside a test is reported that way.
checkasm_failures() {
    local log="$1" clean=/tmp/ck-clean.txt
    # Color is written before the word FAILURE, so strip escape sequences
    # before matching rather than after failing to match.
    sed -E 's/\x1b\[[0-9;]*[A-Za-z]//g' "$log" > "$clean" 2>/dev/null || : > "$clean"
    {
        grep -oE 'FAILURE: [A-Za-z0-9_]+' "$clean" | awk '{print $2}'
        grep -oE '^ - [A-Za-z0-9_.]+ +\[FAILED\]' "$clean" | awk '{print $2}'
    } | sed -E 's/_(c|mmx|mmxext|sse|sse2|sse3|ssse3|sse4|sse42|aesni|clmul|avx|xop|fma3|fma4|avx2|avx512|avx512icl)$//' | sort -u
}

# bench_stage <builddir> <outfile> <seed> -- the same run as checkasm_log, named
# for what the caller does with it: the table the per-function score is read
# from.
bench_stage() {
    checkasm_log "$1" "$2" "$3"
}

# e2e_dump <harness> <outdir> -> one raw plane dump per configuration.
e2e_dump() {
    local hn="$1" out="$2" cfg
    mkdir -p "$out"
    while read -r cfg; do
        [ -n "$cfg" ] || continue
        "$hn" "$cfg" dump "$out/$cfg.raw" > /dev/null 2>&1 \
            || { echo "  harness failed on config $cfg"; return 1; }
    done < /tests/E2E_CONFIGS
}

# e2e_time <harness> <reps> -> one "<config> <best_ms>" line per configuration,
# best over reps.
e2e_time() {
    local hn="$1" reps="$2" cfg r v best
    while read -r cfg; do
        [ -n "$cfg" ] || continue
        best=999999
        for r in $(seq "$reps"); do
            v=$("$hn" "$cfg" 1 2>/dev/null | awk '{print $3}')
            [ -n "$v" ] || v=999999
            best=$(awk -v x="$best" -v y="$v" 'BEGIN{print (y<x)?y:x}')
        done
        echo "$cfg $best"
    done < /tests/E2E_CONFIGS
}

# e2e_time_pair <harness-a> <harness-b> <reps> -> "<config> <best_a> <best_b>"
#
# Both harnesses are timed **in the same round, one after the other**, rather
# than each in its own pass. The first version of this guard timed the base
# before the graft and the candidate after it, minutes apart, and on a shared
# host that is not a comparison: the two passes saw different load, and a
# byte-identical pipeline was measured 1.98x apart. Interleaving makes both
# sides see the same machine, which is the whole point of the ratio.
e2e_time_pair() {
    local ha="$1" hb="$2" reps="$3" cfg r va vb ba bb
    while read -r cfg; do
        [ -n "$cfg" ] || continue
        ba=999999; bb=999999
        for r in $(seq "$reps"); do
            va=$("$ha" "$cfg" 1 2>/dev/null | awk '{print $3}')
            vb=$("$hb" "$cfg" 1 2>/dev/null | awk '{print $3}')
            [ -n "$va" ] || va=999999
            [ -n "$vb" ] || vb=999999
            ba=$(awk -v x="$ba" -v y="$va" 'BEGIN{print (y<x)?y:x}')
            bb=$(awk -v x="$bb" -v y="$vb" 'BEGIN{print (y<x)?y:x}')
        done
        echo "$cfg $ba $bb"
    done < /tests/E2E_CONFIGS
}

# isa_examined <builddir> <stepfile> <outdir> -> count of objects examined,
# with the offending ones printed. See vendor/isa.py.
isa_examined() {
    python3 /tests/vendor/isa.py "$1" "$2" "$3"
}
