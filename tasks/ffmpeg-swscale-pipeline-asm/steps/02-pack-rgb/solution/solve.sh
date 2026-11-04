#!/bin/bash
# Oracle for this milestone: apply the reference implementation and rebuild.
#
# The patch is the difference between two trees that both exist and both build
# -- the level with this milestone's kernels absent and the level with them
# present -- so it cannot conflict with its predecessor and cannot revert it.
set -eu

solution_dir=$(cd "$(dirname "$0")" && pwd)
APP=${HARBOR_APP_DIR:-/app}
SRC="$APP/ffmpeg"

# A crashed oracle and an idle oracle produce the same number, so make a missing
# tool loud at its source instead of letting the run grade a tree that was never
# touched.
for tool in patch make gcc nasm python3; do
    command -v "$tool" >/dev/null || { echo "ORACLE PREFLIGHT FAILED: '$tool' is not installed" >&2; exit 127; }
done

[ -d "$SRC/libswscale/x86" ] || { echo "ORACLE FAILED: $SRC is not the task tree" >&2; exit 1; }

# Verify the patch before touching the workspace.
( cd "$solution_dir" && sha256sum -c SHA256SUMS ) > /dev/null \
    || { echo "ORACLE FAILED: oracle.patch does not match SHA256SUMS" >&2; exit 1; }

patch -p1 -d "$SRC" --batch --forward < "$solution_dir/oracle.patch" \
    || { echo "ORACLE FAILED: oracle.patch did not apply" >&2; exit 1; }

# Rebuild so the applied change is what the verifier grafts, and so a compile
# error names itself here rather than in the grader.
"$APP/build.sh" > /tmp/oracle-build.log 2>&1 \
    || { echo "ORACLE FAILED: rebuild after applying the patch" >&2; tail -20 /tmp/oracle-build.log >&2; exit 1; }

echo "ORACLE APPLIED AND REBUILT: packed RGB repacking"
