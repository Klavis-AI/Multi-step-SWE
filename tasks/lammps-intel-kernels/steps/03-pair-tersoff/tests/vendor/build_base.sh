#!/usr/bin/env bash
# build_base.sh <source-tree> <build-dir>
# Builds the pristine base for this step with the same toolchain the candidate
# is expected to use. Identical flags to LMP_CMAKE in helpers.sh.
set -eu
SRC=$1; BLD=$2
cmake -S "$SRC/cmake" -B "$BLD" \
    -C "$SRC/cmake/presets/basic.cmake" \
    -D PKG_INTEL=on -D PKG_ML-SNAP=on -D PKG_ASPHERE=on -D PKG_DPD-BASIC=on \
    -D BUILD_MPI=no -D BUILD_OMP=on -D CMAKE_BUILD_TYPE=Release \
    -D CMAKE_C_COMPILER=icx -D CMAKE_CXX_COMPILER=icpx \
    -D CMAKE_C_FLAGS="-mavx2 -mfma -mbmi -mbmi2 -O3" \
    -D CMAKE_CXX_FLAGS="-mavx2 -mfma -mbmi -mbmi2 -O3" \
    > /tmp/base-cmake.log 2>&1 \
  || { echo "base configure failed"; tail -30 /tmp/base-cmake.log; exit 1; }
cmake --build "$BLD" -j"${BUILD_JOBS:-4}" > /tmp/base-build.log 2>&1 \
  || { echo "base build failed"; grep -m10 -i error /tmp/base-build.log; exit 1; }
test -x "$BLD/lmp"
