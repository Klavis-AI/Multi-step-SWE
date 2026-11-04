#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: restore this kernel family's upstream StringZilla
# implementation. The patch beside this script is the whole input -- nothing is
# cloned, downloaded or fetched. It was produced by diffing the ladder's
# previous level against this one, both of which are trees this task's own
# stripper produced from the pinned upstream tag, so its contents are exactly
# the public StringZilla code for this family's x86 backends plus the dispatch
# branches that select them.

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Preflight. Without this, a missing tool exits 127 and the step still grades:
# the tree is simply unmodified, correctness passes, and every speedup reads
# ~1.0 -- indistinguishable from a no-op. Fail loudly instead.
for tool in patch cmake gcc sha256sum; do
    command -v "$tool" > /dev/null \
        || { echo "ORACLE PREFLIGHT FAILED: '$tool' is not installed in this environment" >&2; exit 127; }
done

# Verify the payload before touching the workspace.
(
    cd "$solution_dir"
    sha256sum -c SHA256SUMS
)

cd "$app_dir/stringzilla"
patch -p1 --forward --batch < "$solution_dir/oracle.patch"

# Prove the restored tree still builds. Same envelope flags as the grader.
FLAGS="-mavx2 -mbmi -mbmi2 -maes -mpclmul -msse4.2 -mlzcnt -mpopcnt -Wno-error=maybe-uninitialized -Wno-error=uninitialized"
cmake -S "$app_dir/stringzilla" -B "$app_dir/build" -DCMAKE_BUILD_TYPE=Release \
    -DSTRINGZILLA_BUILD_TEST=ON -DSTRINGZILLA_BUILD_BENCHMARK=OFF -DSTRINGZILLA_BUILD_SHARED=OFF \
    -DSTRINGZILLA_USE_SANITIZERS=OFF \
    -DCMAKE_C_FLAGS="$FLAGS" -DCMAKE_CXX_FLAGS="$FLAGS" > /dev/null
cmake --build "$app_dir/build" -j4 > /dev/null
echo "oracle applied and rebuilt: $(basename "$solution_dir")"
