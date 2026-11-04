#!/usr/bin/env bash
# Reference solution for step 6 — the default is IPv6, and no file this
# repository ships still offers an IPv4 address as an example of what it does.
#
# Seventeen files, and two of them are the step and fifteen are its consequence.
#
# `src/redis.h` and `src/redis.c` are the step. `listenToPort()` has always
# entered its loop once with a NULL address when nothing was configured, and a
# NULL node with `AI_PASSIVE` means "the wildcard of every family this host
# has" -- which is how an instance nobody configured ended up with an IPv4
# listener. It now enters that loop with `REDIS_DEFAULT_BINDADDR`, the IPv6
# wildcard spelled out, and `anetTcpServer()`'s existing `anetV6Only()` call
# does the rest: one socket, `AF_INET6`, refusing IPv4 even on a host whose
# `net.ipv6.bindv6only` is 0. `bindaddr_count` deliberately stays zero, so
# `REDIS_BIND_ADDR` -- the *outbound* source address used by replication,
# cluster and Sentinel -- the config rewriter and the process title all still
# get the right answer to "did the operator configure an address".
#
# The other fifteen are the repository describing itself. The annotated
# `redis.conf` documents the new default and stops shipping an active `bind`
# line; `usage()` in `src/redis.c` and `src/redis-benchmark.c`, the Peer-ID
# comment in `src/networking.c`, the log-line examples in `src/replication.c`
# and `src/sentinel.c`, the protocol comments in `src/redis-cli.c` and the
# vendored client's README, test and five examples all named an IPv4 address as
# the thing to connect to or the thing this tree prints. `src/anet.h` and
# `src/anet.c` keep their IPv4 class constants -- a tree that has to recognise
# `127.0.0.0/8` has to write it down -- and say so as prefixes with a length,
# which is what a definition looks like and an example does not.
#
# Nothing here changes what `bind <address>` does. An operator who writes an
# IPv4 address still gets an IPv4 listener, and a name still resolves to every
# family the host has, exactly as step 1 requires.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP="${APP:-/app}"

for f in \
    deps/hiredis/README.md \
    deps/hiredis/examples/example-ae.c \
    deps/hiredis/examples/example-libev.c \
    deps/hiredis/examples/example-libevent.c \
    deps/hiredis/examples/example-libuv.c \
    deps/hiredis/examples/example.c \
    deps/hiredis/test.c \
    redis.conf \
    src/anet.c \
    src/anet.h \
    src/networking.c \
    src/redis-benchmark.c \
    src/redis-cli.c \
    src/redis.c \
    src/redis.h \
    src/replication.c \
    src/sentinel.c \
    ; do
    test -f "$APP/$f" || { echo "missing from the tree: $f" >&2; exit 1; }
    cp "$HERE/tree/$f" "$APP/$f"
done

cd "$APP"
make -j"$(nproc)"
