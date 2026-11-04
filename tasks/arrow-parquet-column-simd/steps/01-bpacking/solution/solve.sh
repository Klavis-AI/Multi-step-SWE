#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: restore Apache Arrow's own optimized bit-packing
# decoder.  The patch beside this script is the whole input -- nothing is
# cloned, downloaded or fetched.  It was produced by diffing level L0 (this
# milestone's base) against level L1 of the task's ladder, so its contents are
# exactly the public Apache Arrow code for this kernel.

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Preflight.  Without this, a missing tool exits 127 and the milestone still
# grades: the tree is simply unmodified, correctness passes, and the speedup
# ratio reads 1.0 -- indistinguishable from a no-op.  A crashed agent and an
# idle agent produce the same number, so failures must be made loud at their
# source.
for tool in patch cmake g++ ccache sha256sum; do
    command -v "$tool" > /dev/null \
        || { echo "ORACLE PREFLIGHT FAILED: '$tool' is not installed in this environment" >&2; exit 127; }
done

# Verify the payload before touching the workspace.
( cd "$solution_dir" && sha256sum -c SHA256SUMS )

cd "$app_dir/arrow"
patch -p1 --forward --batch < "$solution_dir/oracle.patch"

# Prove the restored tree still builds, with the same configuration the checks
# use.
cd "$app_dir"
export BUILD_JOBS="${BUILD_JOBS:-$(nproc)}"
export BUILD_TARGETS="arrow_static arrow_compute_static parquet_static arrow_acero_static"
bash /app/tools/build_arrow.sh "$app_dir/arrow" "$app_dir/build"
echo "oracle applied and rebuilt: $(basename "$solution_dir")"
