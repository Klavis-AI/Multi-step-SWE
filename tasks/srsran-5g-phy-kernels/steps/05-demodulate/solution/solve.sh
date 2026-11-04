#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: restore this kernel's upstream srsRAN
# implementation. The patch beside this script is the whole input -- nothing is
# cloned, downloaded, or fetched. It was produced by diffing the pinned upstream
# commit against this step's base tree, so its contents are exactly the public
# srsRAN code for this kernel.

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

cd "$app_dir/srsran"
patch -p1 --forward --batch < "$solution_dir/oracle.patch"

# Prove the restored tree still builds.
cmake -S "$app_dir/srsran" -B "$app_dir/build" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
      -DMARCH=x86-64-v3 -DENABLE_WERROR=OFF -DENABLE_MKL=OFF -DENABLE_ARMPL=OFF \
      -DENABLE_UHD=OFF -DENABLE_ZEROMQ=OFF -DENABLE_BACKWARD=OFF -DENABLE_PLUGINS=OFF > /dev/null
cmake --build "$app_dir/build" -j4 --target srsran_ldpc srsran_channel_coding \
      srsran_crc_calculator srsran_polar srsran_short_block srsran_channel_modulation \
      srsran_channel_precoder srsran_ofh_compression log_likelihood_ratio srsvec srslog \
      srsran_support srsran_ran > /dev/null
echo "oracle applied and rebuilt: $(basename "$solution_dir")"
