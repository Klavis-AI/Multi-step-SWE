#!/usr/bin/env bash
# Reference solution for step 2 — RFC 8305 on every outbound TCP path.
#
# Two files, and they are the two connect implementations this repository has:
# `src/anet.c` is what the server, the replication link, the cluster bus,
# Sentinel and `redis-benchmark` reach the network through, and
# `deps/hiredis/net.c` is what `redis-cli` and the bundled client library reach
# it through. A tree that fixed one and not the other would be half migrated in
# a way no single caller can show, which is why both are here and why `g04`
# scores an attempt trace from each of them.
#
# Copied whole rather than patched, for the same reason as step 1: a patch that
# does not apply leaves a half-migrated tree that still builds, and that scores
# as a partial success instead of as the packaging error it is.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP="${APP:-/app}"

for f in src/anet.c deps/hiredis/net.c; do
    test -f "$APP/$f" || { echo "missing from the tree: $f" >&2; exit 1; }
    cp "$HERE/tree/$f" "$APP/$f"
done

# `make distclean` and not a plain `make`: the bundled client library is one of
# the changed files, and deps/ is only rebuilt when the top-level Makefile
# decides its settings changed. An incremental build here relinks the same
# libhiredis.a it had before and produces a `redis-cli` with the old connect
# path -- which is precisely the half-migrated tree the note above is about.
cd "$APP"
make distclean >/dev/null 2>&1 || true
make -j"$(nproc)"
