#!/usr/bin/env bash
# Oracle solution, step 01 of 15 -- cloudflare/quiche 0.1.0 (2019-10-01).
#
# The payload is a source snapshot of the 0.1.0 tree: this is the first
# milestone, so there is nothing to diff against.
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

tar --zstd -xf "$here/quiche-0.1.0.tar.zst" -C /app

# No release ships a Cargo.lock -- it is gitignored at every tag -- so the lock
# is authored, and it is the contract between this tree and the offline
# registry cache baked into the verifier image. Installing it last means it
# survives whatever the payload did.
install -m 0644 "$here/Cargo.lock" /app/Cargo.lock
