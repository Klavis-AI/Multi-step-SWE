#!/usr/bin/env bash
# Reference solution for step 1 — build on the current toolchain, and take every
# IP socket's address family from a resolver answer instead of from the source.
#
# The step touches eight files and adds none, so the solution is those eight
# files and the project's own build. They are copied whole rather than patched:
# a patch that does not apply leaves a half-migrated tree that still builds, and
# that scores as a partial success instead of as the packaging error it is.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP="${APP:-/app}"

for f in src/anet.c src/anet.h src/cluster.c src/config.c src/redis.c \
         src/redis.h src/t_string.c deps/hiredis/net.c; do
    test -f "$APP/$f" || { echo "missing from the tree: $f" >&2; exit 1; }
    cp "$HERE/tree/$f" "$APP/$f"
done

# `make distclean` and not a plain `make`: the bundled client library is one of
# the changed files, and deps/ is only rebuilt when the top-level Makefile
# decides its settings changed. An incremental build here relinks the same
# libhiredis.a it had before and produces binaries with the old connect path.
cd "$APP"
make distclean >/dev/null 2>&1 || true
make -j"$(nproc)"
