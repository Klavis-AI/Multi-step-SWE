#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: restore the upstream BitMagic implementation of the
# kernel families this step owns.  The patch beside this script is the whole
# input -- nothing is cloned, downloaded, or fetched.  It was produced by
# diffing this step's base tree against the level below it, so its contents are
# exactly the public BitMagic code for these entry points' `VECT_*` dispatch
# macros and the AVX2 kernels behind them.

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Preflight.  Without this, a missing tool exits 127 and the step still grades:
# the tree is simply unmodified, correctness passes, and the per-entry ratios
# are all 1.0 -- indistinguishable from a no-op.  Fail loudly instead.
for tool in patch cmake g++ make python3 sha256sum; do
    command -v "$tool" > /dev/null \
        || { echo "ORACLE PREFLIGHT FAILED: '$tool' is not installed in this environment" >&2; exit 127; }
done

# Verify the payload before touching the workspace.
(
    cd "$solution_dir"
    sha256sum -c SHA256SUMS
)

cd "$app_dir/bitmagic"
patch -p1 --forward --batch < "$solution_dir/oracle.patch"

# Prove the restored tree still builds: the project's own CMake configure, and
# the milestone's measurement harness, which is header-only and so compiles the
# restored kernels for real.
cmake -S "$app_dir/bitmagic" -B "$app_dir/build" -DCMAKE_BUILD_TYPE=Release \
      -DBMOPTFLAGS=BMAVX2OPT > /dev/null
bash "$app_dir/tools/build.sh" "$app_dir/bitmagic" > /dev/null
echo "oracle applied and rebuilt: $(basename "$solution_dir")"
