#!/usr/bin/env bash
# Build this milestone's measurement harness against the workspace headers.
#
#   bash /app/tools/build.sh [source-tree]        default: /app/bitmagic
#
# The harness is header-only: it is compiled against the tree's own src/, so it
# calls whatever that tree installs for each entry point.  Rebuild it after
# every change to the library.
set -euo pipefail
SRC=${1:-/app/bitmagic}
DIR=$(cd -- "$(dirname -- "$0")" && pwd)
FLAGS="-std=c++17 -O2 -DNDEBUG -march=skylake -mavx2 -mpopcnt -DBMAVX2OPT"
g++ $FLAGS -I "$SRC/src" -o "$DIR/bench_kernels" "$DIR/bench_kernels.cpp"
g++ $FLAGS -I "$SRC/src" -o "$DIR/e2e_bench"   "$DIR/e2e_bench.cpp"
echo "built $DIR/bench_kernels and $DIR/e2e_bench against $SRC"
