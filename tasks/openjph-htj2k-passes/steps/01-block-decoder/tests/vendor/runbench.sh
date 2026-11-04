#!/bin/bash
# Build the per-function benchmark harness against one ladder tree.
#
#   bash runbench.sh <tree> <builddir> <bindir_out>
#
#   <tree>        ladder source tree, e.g. L0 .. L6
#   <builddir>    its CMake build dir, e.g. b-L0 .. b-L6
#   <bindir_out>  where bench_stage is written
#
# The same bench_stage.cpp is compiled against every level, so the only thing
# that differs between two runs is which kernel the ojph::local::* dispatch
# pointer was installed with.
set -eu
cd "$(dirname "$0")"

TREE=${1:?usage: runbench.sh <tree> <builddir> <bindir_out>}
BUILD=${2:?usage: runbench.sh <tree> <builddir> <bindir_out>}
OUT=${3:?usage: runbench.sh <tree> <builddir> <bindir_out>}

INC="-I $TREE/src/core/openjph -I $TREE/src/core/codestream \
     -I $TREE/src/core/transform -I $TREE/src/core/coding"

mkdir -p "$OUT"
g++ -O2 -std=c++14 -o "$OUT/bench_stage" bench_stage.cpp $INC \
    -L "$BUILD/src/core" -lopenjph -Wl,-rpath,"$(cd "$BUILD/src/core" && pwd)" -ldl

echo "built $OUT/bench_stage against $TREE ($BUILD)"

# Optional: show which kernel each dispatch pointer resolves to via dladdr.
#   bash runbench.sh <tree> <builddir> <bindir_out> sym <fnid> <seed>
if [ "${4:-}" = "sym" ]; then
    exec "$OUT/bench_stage" sym "${5:-rev_horz_ana}" "${6:-1}"
fi
