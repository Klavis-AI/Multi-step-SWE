#!/usr/bin/env bash
# Reference solution for step 3 — one spelling for an address, one comparison
# for two of them, and both used everywhere the product writes or reads one.
#
# Ten files. Nine of them are places the tree already joined an address to a
# port with a bare colon, compared two addresses with `strcmp`, or split one at
# the first colon; the tenth, `src/anet.c`, is where the two helpers those nine
# now call actually live -- `anetFormatAddr()` grew a bracketing rule and
# `anetSplitAddr()`/`anetAddrEqual()` are new. A tree that added the helpers and
# left a caller behind would still build and still pass every test that only
# asks whether the helper exists, which is why the callers are here too and why
# `g05` counts join sites per file rather than counting helpers.
#
# `src/redis-trib.rb` is in the list for the same reason and is not compiled:
# the cluster tool parses `ip:port` out of what the server prints, so a server
# that starts bracketing and a tool that still splits at the first colon are a
# migration that breaks itself. `utils/redis-copy.rb` is deliberately NOT in the
# list -- the step leaves it alone, and `g05` scores it as the regression half
# of the same cell.
#
# Copied whole rather than patched, for the same reason as steps 1 and 2: a
# patch that does not apply leaves a half-migrated tree that still builds, and
# that scores as a partial success instead of as the packaging error it is.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP="${APP:-/app}"

for f in src/anet.c src/anet.h src/cluster.c src/networking.c \
         src/redis-benchmark.c src/redis-cli.c src/redis-trib.rb \
         src/redis.c src/replication.c src/sentinel.c; do
    test -f "$APP/$f" || { echo "missing from the tree: $f" >&2; exit 1; }
    cp "$HERE/tree/$f" "$APP/$f"
done

# No `make distclean` here, unlike steps 1 and 2: this step changes nothing
# under `deps/`, so the bundled client library the previous step relinked is
# still the right one and an incremental build rebuilds exactly the nine
# compiled files above. The two earlier steps needed it because
# `deps/hiredis/net.c` was one of the files they changed and deps/ is only
# rebuilt when the top-level Makefile decides its settings changed.
cd "$APP"
make -j"$(nproc)"
