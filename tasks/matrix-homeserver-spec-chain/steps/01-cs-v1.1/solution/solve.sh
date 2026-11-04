#!/usr/bin/env bash
set -euo pipefail
# Reference solution for step 1: a Matrix Client-Server v1.1 homeserver in C17.
HERE="$(cd "$(dirname "$0")" && pwd)"
LOG=/logs/agent/solve.log
mkdir -p "$(dirname "$LOG")" 2>/dev/null || LOG=/tmp/solve.log
exec 3>&1
log () { echo "$@" | tee -a "$LOG" >&3; }

# Mirror rather than merge: /app is not reset between steps, and a file an
# earlier tree needed that this one does not would survive and collide.
rm -rf /app/src
mkdir -p /app/src
# By manifest, not by wildcard: the directory this script was uploaded into is
# not reset between steps either, so it still holds the previous milestone's
# files alongside this one's.
while IFS= read -r f; do
    [ -n "$f" ] || continue
    cp "$HERE/src/$f" "/app/src/$f"
done < "$HERE/src.manifest"
cp -a "$HERE/Makefile" /app/Makefile
log "copied $(ls -1 /app/src | wc -l) files into /app/src from $HERE/src ($(ls -1 "$HERE/src" | wc -l) there)"
cd /app
if ! make >>"$LOG" 2>&1; then
    log "make failed; tail of the build log:"
    tail -40 "$LOG" >&3
    log "src now holds: $(ls -1 /app/src | tr '\n' ' ')"
    exit 1
fi
log "built $(ls -la /app/bin/dendrite)"
test -x /app/bin/dendrite
sync
log "final /app/src holds $(ls -1 /app/src | wc -l) files"

# Leave a handoff note for the next session.
cat > /app/WORKSPACE_HANDOFF.md <<'HANDOFF'
# Workspace handoff

This file is the running log this workspace carries between work sessions.

## Layout

- `src/` — the homeserver, C17. `mxjson` (JSON, and Matrix canonical JSON),
  `mxutil` (buffers, base64, SHA-256, HMAC, Ed25519), `mxhttp` (an HTTP/1.1
  server and a libcurl client), `mxdb` (SQLite), `mxevent` (room versions,
  content and reference hashes, event identifiers, the redaction algorithms and
  the authorisation rules), `mxroom` (the room engine: building, authorising,
  signing and persisting events) and `mxapi_account`, `mxapi_rooms`,
  `mxapi_sync`, `mxapi_media`, `mxapi_misc` (the Client-Server handlers).
- `DENDRITE/` — the published interface this server exposes.
- `selfcheck.sh` — starts the built server the way it is meant to be started and
  walks a handful of requests through it.

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

The Client-Server API of release v1.1: registration and login, profiles, rooms
and room aliases, the room directory, events and their pagination, redaction,
syncing and filtering, the instant-messaging support modules, the content
repository, capabilities and versions. Room versions 1 to 6, each with its own
event format, event identifiers, redaction algorithm and authorisation rules.
Everything is persisted in SQLite and survives a restart.

Known gaps: no identity service, so a third-party invitation this server cannot
back is refused; guests are disabled; there is no application-service API.
HANDOFF

# Leave the workspace as sources, the way the step's meta requirements ask: the
# collected workspace is rebuilt from scratch, so build output is dead weight --
# and Harbor's GKE artifact transfer corrupts binary files, which silently
# truncates everything that follows them in the archive.
cd /app && make clean >/dev/null 2>&1 || true
rm -rf /app/bin /app/media_store /app/*.db /app/*.db-* /app/dendrite.db* 2>/dev/null || true
find /app -name '*.o' -o -name '*.a' -o -name '*.so' | xargs -r rm -f
