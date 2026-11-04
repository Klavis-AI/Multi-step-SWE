#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: restore this stage's upstream Vortex
# implementation. The patch beside this script is the whole input -- nothing is
# cloned, downloaded, or fetched. It is the difference between the tree the
# previous milestone left and the tree this one leaves, so it applies cleanly to
# a cumulative workspace and cannot revert an earlier step.

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Preflight. Without this, a missing tool exits 127 and the step still grades:
# the tree is simply unmodified, correctness passes, and the ratio is 1.0 --
# indistinguishable from a no-op. Fail loudly instead.
for tool in patch cargo python3; do
    command -v "$tool" > /dev/null \
        || { echo "ORACLE PREFLIGHT FAILED: '$tool' is not installed in this environment" >&2; exit 127; }
done

# Verify the payload before touching the workspace.
(
    cd "$solution_dir"
    sha256sum -c SHA256SUMS
)

cd "$app_dir/vortex"
patch -p1 --forward --batch < "$solution_dir/oracle.patch"

# Prove the restored tree still builds.
cargo build --release -p vortex-buffer -p vortex-mask -p vortex-array > /dev/null
echo "oracle applied and rebuilt: $(basename "$solution_dir")"
