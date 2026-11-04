#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: restore upstream's implementation of this stage's
# multi-buffer surface. The patch beside this script is the whole input --
# nothing is cloned, downloaded or fetched. It is the difference between the
# task's base tree and the same tree with this stage restored, so it carries
# exactly the upstream md5_mb sources and build wiring for this
# milestone and nothing else.

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Preflight. Without this, a missing tool exits 127 and the step still grades:
# the tree is simply unmodified, correctness passes, and the ratio is
# indistinguishable from a no-op. Fail loudly instead.
for tool in patch git make gcc python3; do
    command -v "$tool" > /dev/null \
        || { echo "ORACLE PREFLIGHT FAILED: '$tool' is not installed in this environment" >&2; exit 127; }
done

# Verify the payload before touching the workspace.
(
    cd "$solution_dir"
    sha256sum -c SHA256SUMS
)

cd "$app_dir/isal_crypto"
git apply --verbose "$solution_dir/oracle.patch"

# Prove the restored tree still builds.
make -f Makefile.unx lib -j"$(nproc)" > /dev/null
echo "oracle applied and rebuilt: $(basename "$solution_dir")"
