#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: restore this stage's upstream AVX2 implementation.
# The patch beside this script is the whole input -- nothing is cloned,
# downloaded or fetched. It was produced by diffing this step's reference tree
# against the level one stage further up the ladder, so its contents are
# exactly the public mldsa-native code for this stage: the backend's assembly
# files, the shared AVX2 constant block where this stage needs it, the
# `MLD_USE_NATIVE_*` switches and `mld_<op>_native` bodies in
# mldsa/src/native/x86_64/meta.h, and the amalgamation and ABI-checker
# references that go with them.

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Preflight. Without this, a missing tool exits 127 and the step still grades:
# the tree is simply unmodified, correctness passes, and every per-function
# ratio is 1.0 -- indistinguishable from a no-op. Fail loudly instead.
for tool in patch make gcc md5sum python3; do
    command -v "$tool" > /dev/null \
        || { echo "ORACLE PREFLIGHT FAILED: '$tool' is not installed in this environment" >&2; exit 127; }
done

# Verify the payload before touching the workspace.
(
    cd "$solution_dir"
    sha256sum -c SHA256SUMS
)

cd "$app_dir/mldsa-native"
patch -p1 --forward --batch < "$solution_dir/oracle.patch"

# Prove the restored tree still builds.
make -C "$app_dir/mldsa-native" lib OPT=1 AUTO=1 \
    BUILD_DIR="$app_dir/mldsa-native/build" -j"$(nproc)" > /dev/null
echo "oracle applied and rebuilt: $(basename "$solution_dir") ($(md5sum < "$solution_dir/oracle.patch" | cut -c1-12))"
