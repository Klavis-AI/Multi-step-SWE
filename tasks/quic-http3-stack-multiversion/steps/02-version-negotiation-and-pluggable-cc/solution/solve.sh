#!/usr/bin/env bash
# Oracle solution, step 02 of 15 -- cloudflare/quiche 0.3.0 (2020-03-13).
#
# The payload is `git diff 0.1.0..0.3.0` over the upstream repository.
# It was verified at authoring time by applying it to a clean 0.1.0
# checkout and comparing the resulting tree against the real 0.3.0 tree, so
# applying it to what step 01 left behind reproduces the release exactly.
#
# Both payloads were produced with `.git`, `target`, `.github`, `fuzz/corpus`
# and the BoringSSL submodule excluded: history is not the product, build
# output is not source, and the submodule is 400 MB of someone else's C that
# this environment supplies prebuilt through `QUICHE_BSSL_PATH`.
#
# Runs offline. Nothing here touches the network.
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

# Checksum before installing. An edited or truncated payload would otherwise
# surface in Phase 6 as a mysterious partial score instead of as the packaging
# fault it is.
( cd "$here" && sha256sum -c --quiet SHA256SUMS )

patch=$(mktemp)
trap 'rm -f "$patch"' EXIT
zstd -dqc "$here/step-2.patch.zst" > "$patch"

# --binary because the release history moves fixture bytes around; -p1 because
# the diff is repository-rooted and /app is the repository root.
( cd /app && git apply --binary --whitespace=nowarn -p1 "$patch" )

# No release ships a Cargo.lock -- it is gitignored at every tag -- so the lock
# is authored, and it is the contract between this tree and the offline
# registry cache baked into the verifier image. Installing it last means it
# survives whatever the payload did.
install -m 0644 "$here/Cargo.lock" /app/Cargo.lock
