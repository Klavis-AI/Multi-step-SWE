#!/usr/bin/env bash
# Oracle for step 09 (09-mempool-admission) — upstream ZcashFoundation/zebra at v1.0.0-alpha.17
# (e7cec3efc26908cdf44dc5f76216995d8d27ec34).  Deterministic, offline, and self-verifying: it fails loudly
# rather than leaving a half-applied tree.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
APP=${APP:-/app}
cd "$APP"

echo "[oracle 09] verifying payload integrity"
( cd "$HERE" && sha256sum -c SHA256SUMS ) >/dev/null

echo "[oracle 09] applying upstream v1.0.0-alpha.15 -> v1.0.0-alpha.17"
if ! git apply --whitespace=nowarn --unsafe-paths --directory=. "$HERE/release-v1.0.0-alpha.15-to-v1.0.0-alpha.17.patch" 2>/tmp/oracle-09.err; then
    echo "[oracle 09] direct apply failed; retrying without Cargo.lock" >&2
    sed -n '1,40p' /tmp/oracle-09.err >&2
    git apply --whitespace=nowarn --unsafe-paths --exclude=Cargo.lock "$HERE/release-v1.0.0-alpha.15-to-v1.0.0-alpha.17.patch"
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
