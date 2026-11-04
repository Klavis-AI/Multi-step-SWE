#!/bin/bash
# Build the end-to-end sws_scale driver against a build directory.
#
#   ./build_measure.sh [builddir] [outfile]
#
# It links libswscale and libavutil statically, so it must be rebuilt after
# every change to the tree. It is NOT the scored measurement -- milestone
# speedups are scored per function by checkasm, see BENCHMARK.md -- it exists so
# that a change can be checked against the whole sws_scale pipeline, which is
# the thing the task is ultimately about.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=${SRC:-/app/ffmpeg}
BLD=${1:-$SRC/build}
OUT=${2:-$HERE/measure_sws}
gcc -O2 -o "$OUT" "$HERE/measure_sws.c" \
    -I"$BLD" -I"$SRC" \
    "$BLD/libswscale/libswscale.a" "$BLD/libavutil/libavutil.a" \
    -lm -lpthread
echo "built: $OUT"
