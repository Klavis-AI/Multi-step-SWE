#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: restore this stage's upstream OpenJPH
# implementation from the patch beside this script (no network access needed).
# The patch is the diff between this step's base tree and the next ladder
# level: the upstream per-instruction-set translation units, their build
# registration, and the dispatch and declaration lines that install them.

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Preflight. Without this, a missing tool exits 127 and the step still grades:
# the tree is simply unmodified, correctness passes, and the speedup correctly
# reports ~1.0 -- indistinguishable from a no-op. Fail loudly instead.
for tool in patch cmake g++ gcc make; do
    command -v "$tool" > /dev/null \
        || { echo "ORACLE PREFLIGHT FAILED: '$tool' is not installed in this environment" >&2; exit 127; }
done

# Verify the payload before touching the workspace.
(
    cd "$solution_dir"
    sha256sum -c SHA256SUMS
)

cd "$app_dir/ojph"
patch -p1 --forward --batch < "$solution_dir/oracle.patch"

# Prove the restored tree still builds.
rm -rf "$app_dir/build"
cmake -S "$app_dir/ojph" -B "$app_dir/build" -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$app_dir/build" -j"$(nproc)" > /dev/null
echo "oracle applied and rebuilt: $(basename "$solution_dir")"
