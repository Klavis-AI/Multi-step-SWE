#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: restore the upstream SVT-JPEG-XS AVX2
# implementation of this stage's dispatch slots and wire them into the
# encoder's runtime dispatch table.  The patch beside this script is the whole
# input -- nothing is cloned, downloaded, or fetched: it is the diff between the
# pinned upstream commit with this stage (and every later one) removed and the
# same tree with this stage restored, so its contents are exactly the public
# SVT-JPEG-XS code for those slots.

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Preflight.  Without this a missing tool exits 127 and the step still grades:
# the tree is simply unmodified, correctness passes, and the per-function
# ratios read ~1.0 -- indistinguishable from a no-op.  Fail loudly instead.
for tool in patch cmake ninja gcc nasm; do
    command -v "$tool" > /dev/null \
        || { echo "ORACLE PREFLIGHT FAILED: '$tool' is not installed in this environment" >&2; exit 127; }
done

# Verify the payload before touching the workspace.
( cd "$solution_dir" && sha256sum -c SHA256SUMS )

cd "$app_dir/svt-jpeg-xs"
patch -p1 --forward --batch < "$solution_dir/oracle.patch"

# Prove the restored tree still builds.
cmake -S "$app_dir/svt-jpeg-xs" -B "$app_dir/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
    -DBUILD_TESTING=ON -DCMAKE_OUTPUT_DIRECTORY="$app_dir/build/out" > /dev/null
ninja -C "$app_dir/build" > /dev/null
echo "oracle applied and rebuilt: $(basename "$solution_dir")"
