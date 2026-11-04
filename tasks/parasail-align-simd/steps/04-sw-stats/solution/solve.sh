#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: restore the generated vectorized implementations of
# this step's alignment family, exactly as upstream parasail has them at the
# pinned commit.  The patch beside this script is the whole input -- nothing is
# cloned, downloaded or fetched at solve time.
#
# It is the difference between two ladder levels that both already build: this
# step's base and this step's oracle.  That is what makes the ladder monotonic
# by construction -- a patch defined as the difference between two trees that
# both exist cannot conflict with its predecessor or silently revert it.

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Preflight.  Without this a missing tool exits 127, the tree is left
# unmodified, correctness passes, and the step reports ~1.0 -- a crashed oracle
# and an idle one produce the same number.  Fail loudly instead.
for tool in patch meson ninja gcc python3; do
    command -v "$tool" > /dev/null \
        || { echo "ORACLE PREFLIGHT FAILED: '$tool' is not installed in this environment" >&2; exit 127; }
done

# Verify the payload before touching the workspace.
(
    cd "$solution_dir"
    sha256sum -c SHA256SUMS
)

cd "$app_dir/parasail"
patch -p1 --forward --batch < "$solution_dir/oracle.patch"

# Prove the restored tree still builds.
rm -rf "$app_dir/build"
meson setup "$app_dir/build" "$app_dir/parasail" --buildtype=release > /dev/null
ninja -C "$app_dir/build" -j"$(nproc)" > /dev/null
echo "oracle applied and rebuilt: $(basename "$solution_dir")"
