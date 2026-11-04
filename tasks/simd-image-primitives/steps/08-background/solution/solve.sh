#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: restore this family's upstream Simd implementation.
# The patch beside this script is the whole input -- nothing is cloned,
# downloaded or fetched. It was produced by diffing this step's base level
# against the next one, so its contents are exactly the public Simd code for
# this family: the SSE4.1, AVX2, AVX-512BW and (for the non-x86 back ends)
# NEON/SVE2 kernels, the dispatch branches that select them, and the auto-test
# hooks that name them.

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Preflight. Without this, a missing tool exits 127 and the step still grades:
# the tree is simply unmodified, correctness passes, and every ratio reads
# 1.0 -- indistinguishable from a no-op. Fail loudly instead.
for tool in patch cmake ninja python3; do
    command -v "$tool" > /dev/null \
        || { echo "ORACLE PREFLIGHT FAILED: '$tool' is not installed in this environment" >&2; exit 127; }
done

# Verify the payload before touching the workspace.
(
    cd "$solution_dir"
    sha256sum -c SHA256SUMS
)

cd "$app_dir/Simd"
patch -p1 --forward --batch < "$solution_dir/oracle.patch"

# Prove the restored tree still builds.
cmake -G Ninja -S "$app_dir/Simd/prj/cmake" -B "$app_dir/build" \
    -DCMAKE_BUILD_TYPE=Release -DSIMD_TEST=ON -DSIMD_PYTHON=OFF \
    -DSIMD_SYNET=OFF -DSIMD_AVX512=OFF -DSIMD_AVX512VNNI=OFF > /dev/null
cmake --build "$app_dir/build" -j"$(nproc)" > /dev/null
echo "oracle applied and rebuilt: 08-background"
