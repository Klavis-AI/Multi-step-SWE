#!/usr/bin/env bash
# Oracle for step 11 (11-zip401-eviction-conn-limits) — upstream ZcashFoundation/zebra at v1.0.0-beta.0
# (0db35fbee0937e156ef96a2dda963ca786274163).  Deterministic, offline, and self-verifying: it fails loudly
# rather than leaving a half-applied tree.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
APP=${APP:-/app}
cd "$APP"

echo "[oracle 11] verifying payload integrity"
( cd "$HERE" && sha256sum -c SHA256SUMS ) >/dev/null

echo "[oracle 11] applying upstream v1.0.0-alpha.19 -> v1.0.0-beta.0"
if ! git apply --whitespace=nowarn --unsafe-paths --directory=. "$HERE/release-v1.0.0-alpha.19-to-v1.0.0-beta.0.patch" 2>/tmp/oracle-11.err; then
    echo "[oracle 11] direct apply failed; retrying without Cargo.lock" >&2
    sed -n '1,40p' /tmp/oracle-11.err >&2
    git apply --whitespace=nowarn --unsafe-paths --exclude=Cargo.lock "$HERE/release-v1.0.0-alpha.19-to-v1.0.0-beta.0.patch"
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
