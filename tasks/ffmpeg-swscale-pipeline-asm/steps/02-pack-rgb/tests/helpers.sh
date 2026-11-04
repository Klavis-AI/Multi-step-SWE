#!/usr/bin/env bash
# Shared verifier helpers. Per-step copy: the verifier image is built with this
# step's tests/ as the Docker build context, so it cannot COPY from the task
# root. Keep byte-identical across steps.
set -u

# --- protecting the reward from candidate-authored code ---------------------
# Harbor chmods /logs/verifier to 777 as root just before this container
# starts, so *anything* running here can overwrite reward.json -- including
# candidate code this verifier builds or runs. Writing the real score last does
# not help: a detached child outlives the scorer and writes after it. The three
# helpers below are the house pair (harbor_template/README.md, "Protecting the
# reward from the candidate"): harden_reward_dir takes the directory back to
# root:root 0700 after Harbor's chmod, and every command that can execute
# candidate-authored code goes through run_unprivileged, because root can write
# a root-owned directory whatever its mode.

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
#
# This package's verifier deliberately does not call it: it never builds in
# /app -- it grafts the candidate's libswscale/x86/ onto its own copy of the
# base under /work and builds there -- so the build needs no access to /app at
# all, and /app must stay root-owned while candidate code runs.
# `vendor/envelope_scope.py` reads the candidate's seam in /app *after* the
# build to decide whether the instruction-set envelope gate is skipped; a build
# able to write /app could rewrite that seam and defuse the gate.
give_app_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" /app 2>/dev/null || true
}

# CANDIDATE_UNPRIV: 1 while the verifier is working with the candidate's tree.
#
# The verifier image build leaves it at 0 -- nothing the Dockerfile builds or
# runs is candidate-authored -- and test.sh sets it to 1 after this container
# has measured the base and before the candidate's tree is grafted in. From
# there on, every command that can execute candidate code goes through
# run_priv: the make that reads the candidate's libswscale/x86/Makefile (a
# Makefile executes shell), and the checkasm binary and the measurement harness
# (both link candidate objects, whose static initializers run at load).
run_priv() {
    if [ "$CANDIDATE_UNPRIV" = 1 ]; then
        run_unprivileged "$@"
    else
        "$@"
    fi
}
CANDIDATE_UNPRIV=${CANDIDATE_UNPRIV:-0}

# prepare_unprivileged_build <builddir>
#
# Hand the build tree to the unprivileged user before an unprivileged build, or
# it cannot create the objects it compiles. The source tree is deliberately not
# included: an out-of-tree FFmpeg build writes its outputs into the build tree,
# while the source tree holds the portable C reference every ratio is divided by
# and the checkasm framework that decides correctness -- both stay root-owned,
# so code running inside the build cannot rewrite what it is measured against.
# build_tree() retries with the source tree handed over if the build turns out
# to need it, and reports that it did.
prepare_unprivileged_build() {
    local bld="$1"
    mkdir -p "$bld" /tmp/nb
    chown -R "$SCORE_UID:$SCORE_GID" "$bld" /tmp/nb 2>/dev/null || true
}

# give_source_to_unprivileged <srcdir>
#
# The fallback for a build that cannot run with the source tree root-owned: the
# source tree is handed to the unprivileged user and the build is retried. It
# is a deliberate weakening -- candidate-authored shell inside the build can
# then write the portable C reference *in this container* -- so it is never the
# first choice, and it is reported when it happens. It is confined to the
# verifier container, whose tree is discarded when the step ends, and the
# scoring direction stays closed: bench.py divides by min(the candidate's own
# baseline row, the base build's), so pessimizing the reference cannot inflate
# a ratio.
give_source_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" "$1" 2>/dev/null || true
    chmod -R u+rX "$1" 2>/dev/null || true
}
# --- end reward protection --------------------------------------------------

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
# Every value written to reward.json is a number: Harbor parses each key under
# `rewards` as a float, and one string field aborted a nine-step trial after
# step 1 had graded perfectly.
#
# This is deliberately NOT the Harbor template's clamped report(): the reward
# here is not confined to [0, 1].
ORACLE_BASELINE=11.2934060000
report() {
    local correct="${1:-0}" speedup="${2:-0}" isa="${3:-0}" reward
    reward=$(awk -v c="$correct" -v v="$speedup" -v b="$ORACLE_BASELINE" 'BEGIN {
        c += 0; v += 0; b += 0
        if (v < 1) v = 1
        if (c != 1 || v <= 1) { printf "%.6f", 0; exit }
        if (b <= 1 || v >= b) { printf "%.6f", 1; exit }
        r = log(v) / log(b)
        if (r < 0) r = 0; if (r > 1) r = 1
        printf "%.6f", r
    }')
    mkdir -p /logs/verifier
        # oracle binary step reward: 1 only above the hardcoded baseline.
    if [[ "${correct:-0}" != "1" ]] || ! awk -v g="${speedup:-0}" -v b="$ORACLE_BASELINE" 'BEGIN { exit !(g > b) }'; then
        reward="0.000000"
    else
        reward="1.000000"
    fi
printf '{"reward":%s,"correctness":%s,"speedup":%s,"isa_objects":%s}\n' \
        "$reward" "${correct:-0}" "${speedup:-0}" "${isa:-0}" > /logs/verifier/reward.json
    python3 -I - "$reward" "${correct:-0}" "${speedup:-0}" "${isa:-0}" <<'PY'
import json, sys
r = {"reward": float(sys.argv[1]), "correctness": float(sys.argv[2]),
     "speedup": float(sys.argv[3]), "isa_objects": float(sys.argv[4])}
assert all(isinstance(v, float) for v in r.values())
print("REWARD " + json.dumps(r))
PY
    # Reopen to 0755 so a local docker run, which reads this from the host as an
    # ordinary user, can collect it. It stays root-owned, so candidate code --
    # which runs as nobody -- still cannot write it.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
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
#
# When the caller has set CANDIDATE_UNPRIV, the build runs as the unprivileged
# user and the build tree is prepared for it first -- a Makefile is a program,
# and this one is candidate-authored. The Dockerfile's own base build leaves
# the flag at 0 and is byte-for-byte the build it always was.
build_tree() {
    local src="$1" bld="$2" tag
    tag=$(basename "$bld")
    mkdir -p "$bld"
    if [ "$CANDIDATE_UNPRIV" = 1 ]; then
        prepare_unprivileged_build "$bld"
    fi
    if build_tree_attempt "$src" "$bld" "$tag"; then
        return 0
    fi
    if [ "$CANDIDATE_UNPRIV" != 1 ]; then
        return 1
    fi
    # The build needs to write into the source tree, which does not happen in
    # an out-of-tree FFmpeg build (the outputs live in $bld) but can if `make`
    # decides to re-run configure or a rule mirrors a directory into it. Rather
    # than assume and fail an honest tree, hand the source tree over and try
    # once more. Only this retry weakens the source's protection, and it says so.
    echo "  the build failed with the source tree root-owned; retrying with"
    echo "  $src writable by the unprivileged build (give_source_to_unprivileged)"
    give_source_to_unprivileged "$src"
    prepare_unprivileged_build "$bld"
    build_tree_attempt "$src" "$bld" "$tag"
}

# build_tree_attempt <srcdir> <builddir> <tag> -- one configure/make/checkasm
# pass, run the way build_tree describes.
build_tree_attempt() {
    local src="$1" bld="$2" tag="$3"
    if [ ! -f "$bld/config.mak" ]; then
        ( cd "$bld" && run_priv "$src/configure" "${CONFIGURE_FLAGS[@]}" ) \
            > /tmp/configure-$tag.log 2>&1 \
            || { echo "  configure failed"; tail -20 /tmp/configure-$tag.log; return 1; }
    fi
    run_priv make -C "$bld" -j"$(build_jobs)" > /tmp/make-$tag.log 2>&1 \
        || { echo "  build failed"; tail -30 /tmp/make-$tag.log; return 1; }
    run_priv make -C "$bld" -j"$(build_jobs)" checkasm >> /tmp/make-$tag.log 2>&1 \
        || { echo "  checkasm build failed"; tail -30 /tmp/make-$tag.log; return 1; }
    [ -x "$bld/tests/checkasm/checkasm" ] \
        || { echo "  no checkasm binary was produced"; return 1; }
    return 0
}

# check_host_ceiling <ISA.stepN>
#
# The instruction-set envelope's non-enforcing branch (`enforce no`) is a
# statement about the *machine*, not about the source: a milestone whose
# ceiling equals what the host executes has nothing narrower to check, and a
# candidate cannot be credited for reaching wider registers because this host
# cannot run them. The premise is stated in README.md ("The grading host
# reports `avx avx2 sse4_2` and **no AVX-512**") and nowhere else -- so on a
# host wider than the milestone's ceiling the branch would silently credit
# access to a wider machine, which is the fairness failure the envelope exists
# to prevent. This asserts the premise and names it when it does not hold.
#
# It reports; it does not fail the step. Enforcement is not the cure here: the
# reference itself ships kernels wider than the ceiling inside the objects the
# dispatch installs (`x86/rgb_2_rgb.asm`'s AVX-512ICL shuffles sit in the same
# object as the SSSE3 ones milestone 2 installs -- the package's own defect
# list, item 8), so a blanket width check fails the reference solution, and a
# forced failure would zero a tree whose behaviour is correct on a host this
# package was never calibrated for. What must not happen is the *silent* case:
# the check prints one line every run, so a skipped envelope is visible in the
# verifier's output rather than inferred.
check_host_ceiling() {
    local spec="$1" ceiling enforce want wider=""
    # The flag source is overridable so both branches can be exercised off an
    # AVX-512 host; the default is the machine this run is graded on.
    local cpuinfo="${HOST_CPU_FLAGS_FILE:-/proc/cpuinfo}"
    ceiling=$(awk '$1=="ceiling"{print $2}' "$spec" 2>/dev/null)
    enforce=$(awk '$1=="enforce"{print $2}' "$spec" 2>/dev/null)
    if [ "$enforce" = "yes" ]; then
        echo "  envelope premise: not load-bearing here -- this milestone enforces its ceiling"
        return 0
    fi
    case "$ceiling" in
        sse2|ssse3|sse4) want=16 ;;
        avx|avx2)        want=32 ;;
        *)               want=64 ;;
    esac
    if [ "$want" -lt 64 ] && grep -qm1 -E '(^| )avx512[a-z0-9_]*' "$cpuinfo" 2>/dev/null; then
        wider="avx512"
    fi
    if [ "$want" -lt 32 ] && grep -qm1 -E '(^| )avx2( |$)' "$cpuinfo" 2>/dev/null; then
        wider="${wider:+$wider }avx2"
    fi
    if [ -n "$wider" ]; then
        echo "  ENVELOPE PREMISE VIOLATED: this milestone's ceiling is $ceiling, but"
        echo "  this host executes $wider. The non-enforcing envelope branch"
        echo "  assumes the host is at or below the ceiling (README.md, \"The"
        echo "  instruction-set envelope\"); on this host a candidate could install"
        echo "  kernels wider than the reference and be credited for it. Grading"
        echo "  continues as documented for the calibrated host, and this line is"
        echo "  the record that the assumption did not hold."
        return 1
    fi
    echo "  envelope premise: this host executes nothing wider than $ceiling (the milestone's ceiling)"
    return 0
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
    # run_priv: this binary links the candidate's libswscale, so loading it runs
    # candidate static initializers. The redirection is set up by the verifier's
    # own shell, so the log files stay root-owned either way.
    run_priv timeout "${CHECKASM_TIMEOUT:-600}" \
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
    if [ "$CANDIDATE_UNPRIV" = 1 ]; then
        # The harness opens its dump file itself (fopen, "wb"), so the directory
        # has to be writable by the user it runs as. mkdir is the verifier's.
        chown -R "$SCORE_UID:$SCORE_GID" "$out" 2>/dev/null || true
    fi
    while read -r cfg; do
        [ -n "$cfg" ] || continue
        run_priv "$hn" "$cfg" dump "$out/$cfg.raw" > /dev/null 2>&1 \
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
            v=$(run_priv "$hn" "$cfg" 1 2>/dev/null | awk '{print $3}')
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
# is folded in too, since a crash inside a test is reported that way, and it
# keeps its `<module>.` prefix -- such a name can never match a FUNCS entry, so
# the grouped form only ever feeds the whole-suite no-new-failures guard.
#
# The fused-operation module tags its rows with the backend that answered, not
# with the CPU level alone: the x86 backend's rows read `<name>_x86_<cpu
# level>` (vendor/bench.py documents the shape). Stripping only the CPU suffix
# leaves `<name>_x86`, which appears in no FUNCS file -- so for that milestone
# the scoped intersection below was silently empty and the scoped gate examined
# nothing while saying nothing. The backend tag is therefore normalised away
# first, ahead of the CPU suffix. A `_memcpy_` row keeps its tag: that backend
# is present at every level, including the base, and is not this milestone's.
checkasm_failures() {
    local log="$1" clean=/tmp/ck-clean.txt
    local cpu='c|mmx|mmxext|sse|sse2|sse3|ssse3|sse4|sse42|aesni|clmul|avx|xop|fma3|fma4|avx2|avx512|avx512icl'
    # Color is written before the word FAILURE, so strip escape sequences
    # before matching rather than after failing to match.
    sed -E 's/\x1b\[[0-9;]*[A-Za-z]//g' "$log" > "$clean" 2>/dev/null || : > "$clean"
    {
        grep -oE 'FAILURE: [A-Za-z0-9_]+' "$clean" | awk '{print $2}'
        grep -oE '^ - [A-Za-z0-9_.]+ +\[FAILED\]' "$clean" | awk '{print $2}'
    } | sed -E "s/_x86(_($cpu))\$/\1/" \
      | sed -E "s/_($cpu)\$//" | sort -u
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
    if [ "$CANDIDATE_UNPRIV" = 1 ]; then
        # The harness opens its dump file itself (fopen, "wb"), so the directory
        # has to be writable by the user it runs as. mkdir is the verifier's.
        chown -R "$SCORE_UID:$SCORE_GID" "$out" 2>/dev/null || true
    fi
    while read -r cfg; do
        [ -n "$cfg" ] || continue
        run_priv "$hn" "$cfg" dump "$out/$cfg.raw" > /dev/null 2>&1 \
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
            v=$(run_priv "$hn" "$cfg" 1 2>/dev/null | awk '{print $3}')
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
            va=$(run_priv "$ha" "$cfg" 1 2>/dev/null | awk '{print $3}')
            vb=$(run_priv "$hb" "$cfg" 1 2>/dev/null | awk '{print $3}')
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
    python3 -I /tests/vendor/isa.py "$1" "$2" "$3"
}
