#!/usr/bin/env bash
# Oracle for this step: advance /app from upstream tag 0.8.0 to tag 0.9.0.
#
# The payload beside this script is the exact release-to-release tree diff,
# built offline. It applies to the worktree the previous step left behind;
# nothing is cloned or fetched.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cd "$HERE"
sha256sum -c SHA256SUMS

cd /app
rm -rf target
git apply -p1 --binary --whitespace=nowarn "$HERE/release-0.8.0-to-0.9.0.patch"

grep -qE '^version = "0.9.0"' /app/Cargo.toml /app/wild/Cargo.toml \
  || { echo "solve: neither manifest reports version 0.9.0 after the patch" >&2; exit 1; }
for f in TEST_INTERFACE.md WORKSPACE_HANDOFF.md spec/README.md; do
  test -e "/app/$f" || { echo "solve: seeded document /app/$f is missing" >&2; exit 1; }
done
echo "solve: advanced /app from wild 0.8.0 to wild 0.9.0"
