#!/usr/bin/env bash
set -euo pipefail
# Reference solution for step 8: the same C homeserver, with next-generation
# authentication and the v1.14-v1.15 additions.
HERE="$(cd "$(dirname "$0")" && pwd)"
# The agent container is not reset between steps, so /app still holds what the
# previous milestone left there.  This solution is the tree *as of this step*,
# so the source directory is mirrored rather than merged: a file the earlier
# tree needed and this one does not -- the generated stubs for modules that now
# exist, most of all -- would otherwise survive and collide at link time.
rm -rf /app/src
mkdir -p /app/src
# By manifest, not by wildcard: the directory this script was uploaded into is
# not reset between steps either, so it still holds the previous milestone's
# files alongside this one's.
while IFS= read -r f; do
    [ -n "$f" ] || continue
    cp "$HERE/src/$f" "/app/src/$f"
done < "$HERE/src.manifest"
cp "$HERE/Makefile" /app/Makefile
cd /app
make >/tmp/build.log 2>&1 || { tail -40 /tmp/build.log; exit 1; }
test -x /app/bin/dendrite

# Leave a handoff note for the next session.
cat > /app/WORKSPACE_HANDOFF.md <<'HANDOFF'
# Workspace handoff

## Layout

- `src/` — the homeserver, C17. `mxjson` (JSON and Matrix canonical JSON),
  `mxutil` (buffers, base64, SHA-256, HMAC, Ed25519), `mxhttp` (an HTTP/1.1
  server with a TLS listener, and a libcurl client), `mxdb` (SQLite),
  `mxevent` (room versions, hashes, event ids, redaction, the authorisation
  rules), `mxroom` (the room engine: building, authorising, signing and
  persisting events), `mxapi_*` (the Client-Server handlers), `mxfed`
  (the Server-Server API).
- `DENDRITE/` — the published interface this server exposes.
- `selfcheck.sh`, `selfcheck-federation.sh` — start the built server the way it
  is meant to be started and walk requests through it.

## Build

`make` at the top of `/app`. It compiles every `src/*.c` and links
`bin/dendrite` against libsqlite3, libssl, libcrypto, libyaml, libcurl and zlib.

## Run

    bin/dendrite --config <path> --http-bind-address <host:port> \
                 --https-bind-address <host:port> \
                 --tls-cert <path> --tls-key <path>

The configuration is a Dendrite `version: 2` YAML document; unknown keys are
ignored. `global.private_key` is a PEM of type `MATRIX PRIVATE KEY` carrying a
`Key-ID` header and the base64 of the 32-byte Ed25519 seed.

## State of the implementation

See the git-less history in this file and the module list above; each module's
header comment says what it covers. Everything the tests for earlier milestones
exercised is expected to keep working.
HANDOFF

# Leave the workspace as sources, the way the step's meta requirements ask: the
# collected workspace is rebuilt from scratch, so build output is dead weight --
# and Harbor's GKE artifact transfer corrupts binary files, which silently
# truncates everything that follows them in the archive.
cd /app && make clean >/dev/null 2>&1 || true
rm -rf /app/bin /app/media_store /app/*.db /app/*.db-* /app/dendrite.db* 2>/dev/null || true
find /app -name '*.o' -o -name '*.a' -o -name '*.so' | xargs -r rm -f
