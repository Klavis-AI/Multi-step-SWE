#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: restore this operation family's upstream simdutf
# implementation. The patch beside this script is the whole input -- nothing is
# cloned, downloaded, or fetched. It was produced by diffing the pinned tree
# against itself with this family routed at the portable reference, so its
# contents are exactly the public simdutf code for this family: the per-ISA
# kernels, their entries in the backend's implementation.cpp, the
# architecture-generic templates they instantiate, and the shared data tables
# they need.

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Preflight. Without this, a missing tool exits 127 and the step still grades:
# the tree is simply unmodified, correctness passes, and the measurement
# correctly reports ~1.0 -- indistinguishable from a no-op. Fail loudly instead.
for tool in patch cmake g++ python3; do
    command -v "$tool" > /dev/null \
        || { echo "ORACLE PREFLIGHT FAILED: '$tool' is not installed in this environment" >&2; exit 127; }
done

# Verify the payload before touching the workspace.
(
    cd "$solution_dir"
    sha256sum -c SHA256SUMS
)

cd "$app_dir/simdutf"
patch -p1 --forward --batch < "$solution_dir/oracle.patch"

# Prove the restored tree still builds.
cmake -S "$app_dir/simdutf" -B "$app_dir/build" -DCMAKE_BUILD_TYPE=Release \
    -DSIMDUTF_TESTS=ON -DSIMDUTF_BENCHMARKS=OFF > /dev/null
cmake --build "$app_dir/build" -j"$(nproc)" > /dev/null
echo "oracle applied and rebuilt: $(basename "$solution_dir")"
