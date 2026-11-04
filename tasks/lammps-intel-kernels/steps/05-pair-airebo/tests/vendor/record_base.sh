#!/usr/bin/env bash
# record_base.sh <base-build-dir> <workload-dir> <out-prefix>
#
# Runs every generated input against the PRISTINE base with the reference
# styles, and keeps the logs. Those runs are three things at once:
#
#   * the numerator guard -- the denominator of every speedup is the
#     candidate's accelerated row, so without a pristine reference figure a
#     candidate could win by slowing the reference implementation down;
#   * the end-to-end non-regression baseline;
#   * the reference trajectory the candidate's reference path must reproduce.
#
# Recording them here rather than only at grading time keeps the image
# self-checking: a workload that cannot run at image build time fails loudly
# instead of grading every candidate to zero.
set -eu
BLD=$1; WL=$2; OUT=$3
STEP=$(cat /etc/harbor-step-id)
REPS=${BASE_REPS:-2}
CORE=${CORE_REF:-1}
OUTDIR=$OUT
mkdir -p "$OUTDIR"
[ -d "$OUTDIR" ] || { echo "record_base: $OUTDIR is not a directory"; exit 1; }
# source tree for this image is /base (see tests/Dockerfile)
for f in "$WL"/*.in; do
    [ -e "$f" ] || continue
    name=$(basename "$f" .in)
    for r in $(seq 1 "$REPS"); do
        OMP_NUM_THREADS=1 taskset -c "$CORE" "$BLD/lmp" -in "$f" \
            -log "$OUTDIR/$name.base.r$r.log" -screen none \
            -var ROOT /base > "$OUTDIR/$name.base.r$r.out" 2>&1
        grep -q 'Loop time of' "$OUTDIR/$name.base.r$r.log" \
            || { echo "pristine base failed to run $name (rep $r)"; tail -5 "$OUTDIR/$name.base.r$r.out"; exit 1; }
    done
    echo "recorded pristine base: $name"
done
# a marker inside the directory, not on it: $OUT is a directory now, and
# "echo > <dir>" fails the whole image build.
printf '%s\n' "$STEP" > "$OUTDIR/step"
