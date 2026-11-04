#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: restore this stage's upstream mlkem-native AVX2
# implementation. The patch beside this script is the whole input -- nothing is
# cloned, downloaded, or fetched. It was produced by diffing this step's base
# level against the next level of the ladder, so its contents are exactly the
# public mlkem-native code for this stage: the assembly kernels, any constant
# tables they need, and the dispatch block in mlkem/src/native/x86_64/meta.h
# that installs them.

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Preflight. Without this, a missing tool exits 127 and the step still grades:
# the tree is simply unmodified, correctness passes, and the untouched
# functions correctly report ~1.0x -- indistinguishable from a no-op. Fail
# loudly instead.
for tool in patch make gcc python3; do
    command -v "$tool" > /dev/null \
        || { echo "ORACLE PREFLIGHT FAILED: '$tool' is not installed in this environment" >&2; exit 127; }
done

# Verify the payload before touching the workspace.
(
    cd "$solution_dir"
    sha256sum -c SHA256SUMS
)

cd "$app_dir/mlkem-native"
patch -p1 --forward --batch < "$solution_dir/oracle.patch"

# Prove the restored tree still builds.
make lib OPT=1 AUTO=1 -j4 BUILD_DIR="$app_dir/build" > /dev/null
echo "oracle applied and rebuilt: $(basename "$solution_dir")"
