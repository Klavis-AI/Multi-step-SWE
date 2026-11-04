#!/usr/bin/env bash
# Oracle for step 05 (05-history-tree-zip211-redpallas) — upstream ZcashFoundation/zebra at v1.0.0-alpha.12
# (d30b95d0fa9867c58c398bc7c0b94939029d489c).  Deterministic, offline, and self-verifying: it fails loudly
# rather than leaving a half-applied tree.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
APP=${APP:-/app}
cd "$APP"

echo "[oracle 05] verifying payload integrity"
( cd "$HERE" && sha256sum -c SHA256SUMS ) >/dev/null

echo "[oracle 05] applying upstream v1.0.0-alpha.10 -> v1.0.0-alpha.12"
if ! git apply --whitespace=nowarn --unsafe-paths --directory=. "$HERE/release-v1.0.0-alpha.10-to-v1.0.0-alpha.12.patch" 2>/tmp/oracle-05.err; then
    echo "[oracle 05] direct apply failed; retrying without Cargo.lock" >&2
    sed -n '1,40p' /tmp/oracle-05.err >&2
    git apply --whitespace=nowarn --unsafe-paths --exclude=Cargo.lock "$HERE/release-v1.0.0-alpha.10-to-v1.0.0-alpha.12.patch"
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
