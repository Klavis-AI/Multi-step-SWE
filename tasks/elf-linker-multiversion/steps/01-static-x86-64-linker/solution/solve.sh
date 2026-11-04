#!/usr/bin/env bash
# Oracle for step 1: install the upstream reference implementation at tag 0.2.0.
#
# The payload beside this script is a source archive of that tag, built offline.
# Nothing is cloned, fetched or resolved here: the Oracle must be installable
# with no network at all.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cd "$HERE"
sha256sum -c SHA256SUMS

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
tar -xzf "$HERE/wild-0.2.0.tar.gz" -C "$tmp"

# Overlay, not replace: /app already carries the specification tree and the
# handoff documents this step was given, and they must survive into the
# artifact the next step inherits.
cp -a "$tmp/wild-0.2.0/." /app/
rm -rf /app/target

test -f /app/Cargo.toml || { echo "solve: no workspace manifest at /app/Cargo.toml" >&2; exit 1; }
grep -qE '^version = "0.2.0"' /app/wild/Cargo.toml \
  || { echo "solve: /app/wild/Cargo.toml is not version 0.2.0" >&2; exit 1; }
for f in TEST_INTERFACE.md WORKSPACE_HANDOFF.md spec/README.md; do
  test -e "/app/$f" || { echo "solve: seeded document /app/$f did not survive the overlay" >&2; exit 1; }
done
echo "solve: installed wild 0.2.0 into /app"
