#!/usr/bin/env bash
# Shared verifier helpers. Copied verbatim into every step's tests/ (the
# verifier image is built with that step's tests/ as its Docker build context,
# so it cannot reach the task root). Keep the copies byte-identical: a
# divergence here is a divergence in what every step measures.
set -u

# ---------------------------------------------------------------------------
# Candidate code protection.
#
# the runner chmods /logs/verifier to 777 as root just before this container
# starts, so anything running here can overwrite reward.json -- including
# candidate code this verifier builds and runs. Writing the score last does not
# help: a detached child outlives the scorer and writes after it.
#
# This verifier compiles the candidate's tree and executes the binary it
# produces, so both halves of the defence are needed: lock the directory to
# root, and run every piece of candidate code as an unprivileged user, because
# root can write a root-owned directory whatever its mode.
# ---------------------------------------------------------------------------
SCORE_UID=65534
SCORE_GID=65534

harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 700 /logs/verifier
}

give_app_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" /app 2>/dev/null || true
}

# run_unprivileged <cmd> [args...]
run_unprivileged() {
    setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        env HOME=/tmp TMPDIR=/tmp "$@"
}

# make_writable_by_unprivileged <dir> -- candidate builds write here
make_writable_by_unprivileged() {
    mkdir -p "$1"
    chown -R "$SCORE_UID:$SCORE_GID" "$1" 2>/dev/null || true
}

set -u

# ---------------------------------------------------------------------------
# report <correctness 0|1> <speedup> [<speedup> ...]
#
#   reward = 1 if correctness == 1 and geomean > ORACLE_BASELINE, else 0
#   geomean = exp( mean_f( ln(max(1, speedup_f)) ))
#
# One speedup per scoped style. The geometric mean is the right aggregator for
# ratios and it penalises neglect: a style left on the reference path
# contributes ln(1) = 0 and drags the step down, so a large win on one style
# cannot hide doing nothing on another.
#
# max(1, .) floors a regression at 0 rather than going negative. Correctness is
# binary and multiplies, so a tree that changes behaviour fails however fast.
#
# ORACLE_BASELINE is this step's hardcoded oracle speedup: a step at or above it
# passes the step, and the reward is confined to [0, 1].
#
# $VERBATIM, when set to a number, is emitted alongside as the non-scored
# similarity diagnostic. Every value under `rewards` must be numeric: the runner
# parses them as floats, and a string field aborts the trial.
ORACLE_BASELINE=2.6729000000
report() {
    local correct="${1:-0}"; shift || true
    local reward geo extra=""
    geo=$(awk 'BEGIN {
        n = ARGC - 1; if (n <= 0) { printf "%.4f", 1; exit }
        s = 0
        for (i = 1; i <= n; i++) { v = ARGV[i] + 0; if (v < 1) v = 1; s += log(v) }
        g = sprintf("%.4f", exp(s / n)) + 0
        printf "%.4f", g
    }' "$@")
    if [ -n "${VERBATIM:-}" ]; then
        extra=$(awk -v v="$VERBATIM" 'BEGIN{ printf ",\"verbatim_fraction\":%.4f", v+0 }')
    fi
    mkdir -p /logs/verifier
        # oracle binary step reward: 1 only above the hardcoded baseline.
    if [[ "${correct:-0}" != "1" ]] || ! awk -v g="${geo:-0}" -v b="$ORACLE_BASELINE" 'BEGIN { exit !(g > b) }'; then
        reward="0.000000"
    else
        reward="1.000000"
    fi
printf '{"reward":%s,"correctness":%s,"geomean_speedup":%s%s}\n' \
        "$reward" "${correct:-0}" "$geo" "$extra" > /logs/verifier/reward.json
    # Reopen so a local docker run can collect the reward from the host as an
    # ordinary user. Still root-owned, so the candidate still cannot write it.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    echo "REWARD correctness=${correct} speedups=[$*] geomean=${geo} reward=${reward}"
    exit 0
}

# LAMMPS build flags. Identical on both sides of every comparison, and identical
# to the flags the agent image documents.
LMP_CFLAGS="-mavx2 -mfma -mbmi -mbmi2 -O3"

# build_tree <source-tree> <build-dir>
build_tree() {
    local src=$1 bld=$2
    make_writable_by_unprivileged "$bld"
    run_unprivileged cmake -S "$src/cmake" -B "$bld" \
        -C "$src/cmake/presets/basic.cmake" \
        -D PKG_INTEL=on -D PKG_ML-SNAP=on -D PKG_ASPHERE=on -D PKG_DPD-BASIC=on \
        -D BUILD_MPI=no -D BUILD_OMP=on -D CMAKE_BUILD_TYPE=Release \
        -D CMAKE_C_COMPILER=icx -D CMAKE_CXX_COMPILER=icpx \
        -D CMAKE_C_FLAGS="$LMP_CFLAGS" -D CMAKE_CXX_FLAGS="$LMP_CFLAGS" \
        > /tmp/cmake.log 2>&1 \
        || { echo "  cmake configure failed for $src"; tail -20 /tmp/cmake.log; return 1; }
    run_unprivileged cmake --build "$bld" -j"${BUILD_JOBS:-4}" > /tmp/build.log 2>&1 \
        || { echo "  build failed for $src"; grep -m10 -i 'error' /tmp/build.log; return 1; }
    [ -x "$bld/lmp" ] || { echo "  no lmp binary in $bld"; return 1; }
}

# row_of <log> <row> -- the row's avg time from the LAST timing table in the log
row_of() {
    awk -v r="$2" '$1==r && $2=="|" {v=$3} END{if (v!="") print v}' "$1" 2>/dev/null
}

# rows_of_min <row> <log>... -- minimum of that row over the logs
rows_of_min() {
    local row=$1; shift; local f
    for f in "$@"; do [ -f "$f" ] && row_of "$f" "$row"; done | sort -g | head -1
}

# loop_of <log> -- total loop time in seconds from the last "Loop time of" line
loop_of() {
    awk '/^Loop time of/ {v=$4} END{if (v!="") print v}' "$1" 2>/dev/null
}
loop_median() {
    local f
    for f in "$@"; do [ -f "$f" ] && loop_of "$f"; done | sort -g | awk '{a[NR]=$1} END{if(NR)print a[int((NR+1)/2)]}'
}

# rows_of_median <row> <log>... -- median of that row over the logs
rows_of_median() {
    local row=$1; shift; local f
    for f in "$@"; do [ -f "$f" ] && row_of "$f" "$row"; done | sort -g | awk '{a[NR]=$1} END{if(NR)print a[int((NR+1)/2)]}'
}

# run_input <lmp> <src-tree> <input> <sfx> <core> <reps> <tag> <outdir>
# Runs <input> <reps> times, pinned to one core.
#
# sfx=0 is the reference configuration. sfx=1 turns LAMMPS's own style suffix
# on: every style the input names is resolved as "<style>/intel" first and falls
# back to "<style>" when the accelerated twin is absent. That fallback is the
# mechanism the score is built on -- a tree that has not written the style still
# runs, on the reference path, and measures 1.0 rather than crashing.
run_input() {
    local lmp=$1 src=$2 input=$3 sfx=$4 core=$5 reps=$6 tag=$7 outdir=$8 r
    local sfxargs=()
    [ "$sfx" = 1 ] && sfxargs=(-pk intel 0 omp 0 mode double -sf intel)
    for r in $(seq 1 "$reps"); do
        rm -f "$outdir/$tag.r$r.log"
        OMP_NUM_THREADS=1 run_unprivileged taskset -c "$core" "$lmp" -in "$input" \
            -log "$outdir/$tag.r$r.log" -screen none -var ROOT "$src" \
            "${sfxargs[@]}" > "$outdir/$tag.r$r.out" 2>&1
        if [ ! -s "$outdir/$tag.r$r.log" ] || ! grep -q 'Loop time of' "$outdir/$tag.r$r.log"; then
            echo "  RUN FAILED: tag=$tag rep=$r sfx=$sfx input=$input"
            tail -4 "$outdir/$tag.r$r.out"
            return 1
        fi
    done
}

# first_log <glob> <dir> -- first matching file, empty when none matches.
# Gates use it so that a missing log is a named failure rather than an awk
# error swallowed by a comparison that then reports agreement.
first_log() {
    local g=$1 d=$2 f
    for f in $g; do
        [ -f "$f" ] && { echo "$f"; return 0; }
    done
    return 1
}

# thermo_compare_window <logA> <logB> <steps> <abs> <rel>
# Compares the first <steps> printed timesteps of two runs of the same input.
# Columns are matched by position after the step index; the printed order is
# fixed by the inputs, which are verifier-owned.
#
# Output is always two lines:
#   line 1  "STEPS <n>" -- how many timesteps were actually compared inside the
#           window. Callers fail their gate when this is 0 or is not a number,
#           so that a log with no comparable numeric rows cannot read as
#           agreement: "0 divergences" over an empty comparison is not a pass.
#   line 2  the worst-offender description when the difference exceeds
#           abs + rel*|reference|, empty when it does not.
#
# The window is bounded on purpose: a legitimate reimplementation that
# accumulates forces in a different order is still correct, and molecular
# dynamics amplifies a rounding-level difference exponentially, so an unbounded
# comparison would fail a correct tree for being a correct tree.
thermo_compare_window() {
    local a=$1 b=$2 steps=$3 abst=$4 relt=$5
    awk -v steps="$steps" -v abst="$abst" -v relt="$relt" '
        function allnumeric(line,  k, f, arr) {
            k = split(line, arr, " ")
            for (f = 1; f <= k; f++) if (arr[f] !~ /^-?[0-9]/) return 0
            return (k >= 5)
        }
        FNR == 1 { n++; next }
        allnumeric($0) {
            if (n == 1) { A[++na] = $0; sa[na] = $1 }
            else        { B[++nb] = $0; sb[nb] = $1 }
        }
        END {
            for (i = 1; i <= nb; i++) Bs[sb[i]] = B[i]
            worst = 0; worstline = ""; div = ""; cmp = 0
            for (i = 1; i <= na; i++) {
                if (sa[i] + 0 > steps) continue
                if (!(sa[i] in Bs)) { div = "step " sa[i] " missing from the compared log"; break }
                cmp++
                na2 = split(A[i], x, " ")
                nb2 = split(Bs[sa[i]], y, " ")
                for (j = 2; j <= na2 && j <= nb2; j++) {
                    d = x[j] - y[j]; if (d < 0) d = -d
                    lim = abst + relt * (y[j] < 0 ? -y[j] : y[j])
                    if (lim <= 0) continue
                    r = d / lim
                    if (r > worst) {
                        worst = r
                        worstline = "step " sa[i] " column " j ": reference " x[j] " accelerated " y[j]
                    }
                }
            }
            printf "STEPS %d\n", cmp
            if (div != "") printf "%s\n", div
            else if (worst > 1.0)
                printf "THERMO DIVERGENCE beyond budget: %s (%.2fx the allowed bound)\n", worstline, worst
        }' "$a" "$b"
}

# verbatim_fraction <candidate-tree> <upstream-tree> <file-list>
# Non-scored similarity diagnostic: of the lines the candidate added to this
# step's files, what fraction appears verbatim in upstream's copy of the same
# file. Reads ~1.0 for the oracle and ~0.0 for a tree that adds nothing.
# It is a verbatim-line measure: cosmetically rewritten code reads low, so it is
# a lower bound on copying, never a clean bill of health.
verbatim_fraction() {
    local ct=$1 ut=$2 list=$3
    [ -f "$list" ] || { echo "0"; return; }
    awk 'NR==FNR { up[$0]=1; next } { n++; if ($0 in up) k++ } END { printf "%.4f", (n?k/n:0) }' \
        <(grep -v '^#' "$list" | grep -v '^$' | while read -r f; do
              [ -f "$ut/$f" ] && grep -vE '^[[:space:]]*$' "$ut/$f"
          done | sed 's/^[[:space:]]*//;s/[[:space:]]*$//' | sort -u) \
        <(grep -v '^#' "$list" | grep -v '^$' | while read -r f; do
              [ -f "$ct/$f" ] && grep -vE '^[[:space:]]*$' "$ct/$f"
          done | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
}
