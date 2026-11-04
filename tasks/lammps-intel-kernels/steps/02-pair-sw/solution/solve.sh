#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: restore this milestone's accelerated style(s) from
# the pinned upstream tag. The patch beside this script is the whole input --
# nothing is cloned, downloaded or fetched, so it applies offline.
#
# Its contents are exactly upstream's public implementation of these files at
# the pin and nothing else.

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Preflight. Without this, a missing tool exits 127 and the milestone still
# reports success: the tree is simply unmodified, correctness passes, and the
# measurement correctly reports ~1.0 -- indistinguishable from a no-op. Fail
# loudly instead.
for tool in patch cmake icpx icx sha256sum; do
    command -v "$tool" > /dev/null \
        || { echo "ORACLE PREFLIGHT FAILED: '$tool' is not installed in this environment" >&2; exit 127; }
done

# Verify the payload before touching the workspace.
(
    cd "$solution_dir"
    sha256sum -c SHA256SUMS
)

cd "$app_dir/lammps"
patch -p1 --forward --batch < "$solution_dir/oracle.patch"

# Prove the restored tree still builds.
cmake -S "$app_dir/lammps/cmake" -B "$app_dir/build" \
    -C "$app_dir/lammps/cmake/presets/basic.cmake" \
    -D PKG_INTEL=on -D PKG_ML-SNAP=on -D PKG_ASPHERE=on -D PKG_DPD-BASIC=on \
    -D BUILD_MPI=no -D BUILD_OMP=on -D CMAKE_BUILD_TYPE=Release \
    -D CMAKE_C_COMPILER=icx -D CMAKE_CXX_COMPILER=icpx \
    -D CMAKE_C_FLAGS="-mavx2 -mfma -mbmi -mbmi2 -O3" \
    -D CMAKE_CXX_FLAGS="-mavx2 -mfma -mbmi -mbmi2 -O3" > /dev/null
cmake --build "$app_dir/build" -j"${BUILD_JOBS:-4}" > /dev/null
echo "oracle applied and rebuilt: $(basename "$solution_dir")"
