#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: restore the upstream implementation of this
# strategy family. The patch beside this script is the whole input -- nothing is
# cloned, downloaded, or fetched. It was produced by diffing this step's base
# tree against the next ladder level, so its contents are exactly the public
# uvg266 code for this family: the optimized source, its tables header, its
# build entry, and the #include and registration call in the family dispatcher.

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Preflight. Without this, a missing tool exits 127 and the step still grades:
# the tree is simply unmodified, correctness passes, and the per-function ratios
# are all 1.0 -- indistinguishable from a no-op. Fail loudly instead.
for tool in patch cmake make gcc python3; do
    command -v "$tool" > /dev/null \
        || { echo "ORACLE PREFLIGHT FAILED: '$tool' is not installed in this environment" >&2; exit 127; }
done
[ -d "$app_dir/uvg266" ] || { echo "ORACLE PREFLIGHT FAILED: $app_dir/uvg266 is missing" >&2; exit 127; }

# Verify the payload before touching the workspace.
(
    cd "$solution_dir"
    sha256sum -c SHA256SUMS
)

cd "$app_dir/uvg266"
patch -p1 --forward --batch < "$solution_dir/oracle.patch"

# Prove the restored tree still builds.
cmake -S "$app_dir/uvg266" -B "$app_dir/build" -DCMAKE_BUILD_TYPE=Release \
    -DUVG_BUILD_TESTS=ON > /dev/null
make -C "$app_dir/build" -j"$(nproc)" > /dev/null
echo "oracle applied and rebuilt: $(basename "$solution_dir")"
