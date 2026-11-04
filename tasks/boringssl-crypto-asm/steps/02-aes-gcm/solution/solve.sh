#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: restore the AES-GCM implementation: the GHASH carry-less
# multiply, the AVX2 and AVX-512 variants upstream ships with it, the
# fused stitched loop, and the dispatch and key pairing that install them. The patch beside this script is the whole input -- nothing is
# cloned, downloaded, or fetched. It was produced by diffing the two adjacent
# ladder levels, both of which were built and tested, so it is exactly the
# perlasm, its pre-generated assembly, its build wiring and its dispatch, and
# nothing else.

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Preflight. Without this, a missing tool exits 127 and the step still grades:
# the tree is simply unmodified, correctness passes, every ratio is 1.0, and a
# crashed oracle is indistinguishable from an idle one. Fail loudly instead.
for tool in patch cmake ninja gcc g++; do
    command -v "$tool" > /dev/null \
        || { echo "ORACLE PREFLIGHT FAILED: '$tool' is not installed in this environment" >&2; exit 127; }
done

# Verify the payload before touching the workspace.
( cd "$solution_dir" && sha256sum -c SHA256SUMS )

cd "$app_dir/boringssl"
patch -p1 --forward --batch < "$solution_dir/oracle.patch"

# Prove the restored tree still builds.
cmake -S "$app_dir/boringssl" -B "$app_dir/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$app_dir/build" -j4 --target crypto > /dev/null
echo "oracle applied and rebuilt: $(basename "$solution_dir")"
