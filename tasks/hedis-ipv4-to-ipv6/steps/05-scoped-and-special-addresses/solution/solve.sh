#!/usr/bin/env bash
# Reference solution for step 5 — an address is classified by its value, a peer
# is named by a unicast address, and a mapped address is the address it embeds.
#
# Five files, and four of them only ask a question the fifth now answers.
#
# `src/anet.c` and `src/anet.h` are the classifier. `anetLiteralToSockaddr` now
# parses into the one family that can express every address there is, with
# `AI_V4MAPPED`, so an IPv4 literal and its mapped spelling become the same
# sixteen bytes and `anetAddrIsEqual` — whose body did not change — starts
# calling them one address. `anetAddrClass` reads those bytes against the
# prefixes RFC 4291, RFC 4193 and the IANA IPv4 registry define, so an address
# never seen before is classified on its first sighting; `anetAddrIsPeer` is the
# one-line consequence, and `anetAddrClassName` is what the refusals say.
#
# `src/cluster.c`, `src/replication.c` and `src/sentinel.c` are the three places
# this repository is handed an address that is supposed to name another machine.
# The cluster's check sits in `clusterStartHandshake` rather than in the
# `CLUSTER MEET` handler because that is also where the addresses learned from
# gossip arrive; Sentinel's sits in `createSentinelAddr` for the same reason,
# which is what makes the `sentinel monitor` *directive* refuse as well as the
# command. Each refusal gets its own errno, `EDESTADDRREQ`, so that it can be
# told apart from the `EINVAL` the port check has always raised and the historic
# message for that case is left exactly as it was.
#
# Nothing here opens a socket, so nothing here changes which family anything
# runs on: the parser is a parser.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP="${APP:-/app}"

for f in \
    src/anet.c \
    src/anet.h \
    src/cluster.c \
    src/replication.c \
    src/sentinel.c \
    ; do
    test -f "$APP/$f" || { echo "missing from the tree: $f" >&2; exit 1; }
    cp "$HERE/tree/$f" "$APP/$f"
done

cd "$APP"
make -j"$(nproc)"
