#!/usr/bin/env bash
# Reference solution for step 4 — run where the kernel has no IPv4, and stop
# shipping IPv4 addresses as places to go.
#
# Twenty-three files, and they divide the way the step does.
#
# `src/anet.c` is the whole of the first half: a family the kernel does not
# implement is skipped rather than fatal, so a `bind` line whose name resolves
# to an address of both families comes up on the one that exists. Nothing else
# compiled changes, because nothing else decides it -- every listener in this
# tree is opened through `anetTcpServer()` and every connection through
# `anetTcpGenericConnect()`.
#
# The other twenty-two are the second half: every file the repository ships that
# something *acts on* and that named an IPv4 address. `src/redis-cli.c` and
# `src/redis-benchmark.c` default to a host rather than to a quad; the annotated
# `redis.conf` and `sentinel.conf` keep every directive they document and change
# the address in it; the Tcl suite's assets, helpers and instance definitions,
# the cluster helper under `utils/`, the two `utils/` scripts and the vendored
# client's own Makefile do the same. A commented-out line is a place the
# repository goes and is corrected rather than deleted, which is why the file
# count here is twenty-three and not three.
#
# Copied whole rather than patched, for the same reason as steps 1 to 3: a patch
# that does not apply leaves a half-migrated tree that still builds, and that
# scores as a partial success instead of as the packaging error it is.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP="${APP:-/app}"

for f in \
    deps/hiredis/Makefile \
    redis.conf \
    sentinel.conf \
    src/anet.c \
    src/redis-benchmark.c \
    src/redis-cli.c \
    tests/assets/default.conf \
    tests/cluster/cluster.tcl \
    tests/cluster/tests/03-failover-loop.tcl \
    tests/cluster/tests/04-resharding.tcl \
    tests/cluster/tests/05-slave-selection.tcl \
    tests/cluster/tests/06-slave-stop-cond.tcl \
    tests/cluster/tests/10-manual-failover.tcl \
    tests/cluster/tests/includes/init-tests.tcl \
    tests/instances.tcl \
    tests/support/cluster.tcl \
    tests/support/redis.tcl \
    tests/support/util.tcl \
    tests/test_helper.tcl \
    tests/unit/scripting.tcl \
    utils/create-cluster/create-cluster \
    utils/redis-sha1.rb \
    utils/speed-regression.tcl \
    ; do
    test -f "$APP/$f" || { echo "missing from the tree: $f" >&2; exit 1; }
    cp "$HERE/tree/$f" "$APP/$f"
done

# The executable bit is part of what `utils/create-cluster/create-cluster` is:
# it is run as a program by an operator and by the project's own documentation,
# and a copy that landed without it would be a file the tree still ships and no
# longer runs.
chmod +x "$APP/utils/create-cluster/create-cluster"

# `make distclean` and not a plain `make`: `deps/hiredis/Makefile` is one of the
# changed files, and deps/ is only rebuilt when the top-level Makefile decides
# its settings changed. An incremental build here relinks the same libhiredis.a
# it had before, from the Makefile this step just replaced.
cd "$APP"
make distclean >/dev/null 2>&1 || true
make -j"$(nproc)"
