#!/usr/bin/env bash
# Oracle solution for the v0.20.9 milestone.
#
# The Oracle is the real upstream release: it lays down the tag's complete
# source tree and the build script the task's interface requires. It is
# deliberately not a patch. Patches must apply byte-exactly to a tree that has
# made a Harbor artifact round trip, and the v0.25.0 -> v0.26.2 restructure
# deletes cli/ and creates crates/, which a patch expresses as thousands of
# renames -- a fragile way to state "the tree is now this".
#
# Because the tree is replaced rather than merged, anything agent-visible has
# to survive explicitly. PRESERVE is that list, and it is the same list the
# task's workdir material is built from; no upstream tag contains any of these
# paths, so nothing collides on the way back in.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRESERVE=(spec TEST_INTERFACE.md WORKSPACE_HANDOFF.md timer.sh)

cd /app

keep="$(mktemp -d /tmp/oracle-keep.XXXXXX)"
for item in "${PRESERVE[@]}"; do
    [ -e "/app/$item" ] && cp -a "/app/$item" "$keep/"
done

# Clear the release tree. `find -mindepth 1 -maxdepth 1` rather than `rm -rf *`
# so dotfiles go too: a stale .cargo/config.toml from an earlier milestone
# would silently change how the next one builds.
find /app -mindepth 1 -maxdepth 1 -exec rm -rf {} +

tar -xzf "$HERE/release.tar.gz" -C /app

for item in "${PRESERVE[@]}"; do
    [ -e "$keep/$item" ] && cp -a "$keep/$item" "/app/$item"
done
rm -rf "$keep"

# The interface contract from TEST_INTERFACE.md: an executable /app/build.sh
# that produces an executable /app/bin/tree-sitter. The verifier runs this
# unprivileged, with a warm crate cache and CARGO_NET_OFFLINE honoured if set.
cat > /app/build.sh <<'BUILD'
#!/bin/sh
set -e
cd /app
cargo build --release --locked
mkdir -p /app/bin
cp target/release/tree-sitter /app/bin/tree-sitter
BUILD
chmod +x /app/build.sh

echo "oracle: staged v0.20.9 into /app"
