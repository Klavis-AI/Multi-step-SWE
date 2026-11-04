#!/usr/bin/env bash
# Oracle for step 03 (03-v5-sapling-orchard-types) — upstream ZcashFoundation/zebra at v1.0.0-alpha.8
# (500dc2e5110a14f7ef4949f974252142c4c07f9f).  Deterministic, offline, and self-verifying: it fails loudly
# rather than leaving a half-applied tree.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
APP=${APP:-/app}
cd "$APP"

echo "[oracle 03] verifying payload integrity"
( cd "$HERE" && sha256sum -c SHA256SUMS ) >/dev/null

echo "[oracle 03] applying upstream v1.0.0-alpha.6 -> v1.0.0-alpha.8"
if ! git apply --whitespace=nowarn --unsafe-paths --directory=. "$HERE/release-v1.0.0-alpha.6-to-v1.0.0-alpha.8.patch" 2>/tmp/oracle-03.err; then
    echo "[oracle 03] direct apply failed; retrying without Cargo.lock" >&2
    sed -n '1,40p' /tmp/oracle-03.err >&2
    git apply --whitespace=nowarn --unsafe-paths --exclude=Cargo.lock "$HERE/release-v1.0.0-alpha.6-to-v1.0.0-alpha.8.patch"
fi

echo "[oracle] pinning Cargo.lock and licences to the tag"
cp -f "$HERE/Cargo.lock"     "$APP/Cargo.lock"
cp -f "$HERE/LICENSE-APACHE" "$APP/LICENSE-APACHE"
cp -f "$HERE/LICENSE-MIT"    "$APP/LICENSE-MIT"

echo "[oracle] verifying the tree reproduces the tag byte for byte"
cd "$APP"
if ! sha256sum -c --quiet "$HERE/TREE.sha256" > /tmp/oracle-verify.log 2>&1; then
    echo "[oracle] FAILED: the applied tree does not reproduce the tag" >&2
    grep -c . /tmp/oracle-verify.log | sed 's/^/  mismatching or missing files: /' >&2
    head -20 /tmp/oracle-verify.log >&2
    exit 1
fi
echo "[oracle] tree matches the tag: $(wc -l < "$HERE/TREE.sha256") files verified"
