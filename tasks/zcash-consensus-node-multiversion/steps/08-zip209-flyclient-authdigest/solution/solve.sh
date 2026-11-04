#!/usr/bin/env bash
# Oracle for step 08 (08-zip209-flyclient-authdigest) — upstream ZcashFoundation/zebra at v1.0.0-alpha.15
# (13651d432d230959b6c7db043e82a6cc083df641).  Deterministic, offline, and self-verifying: it fails loudly
# rather than leaving a half-applied tree.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
APP=${APP:-/app}
cd "$APP"

echo "[oracle 08] verifying payload integrity"
( cd "$HERE" && sha256sum -c SHA256SUMS ) >/dev/null

echo "[oracle 08] applying upstream v1.0.0-alpha.14 -> v1.0.0-alpha.15"
if ! git apply --whitespace=nowarn --unsafe-paths --directory=. "$HERE/release-v1.0.0-alpha.14-to-v1.0.0-alpha.15.patch" 2>/tmp/oracle-08.err; then
    echo "[oracle 08] direct apply failed; retrying without Cargo.lock" >&2
    sed -n '1,40p' /tmp/oracle-08.err >&2
    git apply --whitespace=nowarn --unsafe-paths --exclude=Cargo.lock "$HERE/release-v1.0.0-alpha.14-to-v1.0.0-alpha.15.patch"
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
