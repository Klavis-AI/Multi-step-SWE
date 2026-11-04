#!/usr/bin/env bash
# Oracle solution for step 01 of the uv release chain.
#
#   transition : source archive of tag 0.1.0 unpacked into /app
#   payload    : uv-0.1.0-tree.tar.gz
#
# This script is fully offline: it clones nothing, downloads nothing, fetches
# nothing, and invokes no build tool.  It uses only sha256sum, gzip, tar/git
# and shell builtins against the payload sitting next to it on disk.
set -Eeuo pipefail

# Operates on /app.  ORACLE_APP_DIR exists only so the chain can be verified
# against a scratch "/app-like" directory without root; unset (the normal case,
# and the only case in the grading container) this is exactly /app.
APP="${ORACLE_APP_DIR:-/app}"
STEP="01"
TARGET_VERSION="0.1.0"
PAYLOAD="uv-0.1.0-tree.tar.gz"
VERSION_FILE="crates/uv/Cargo.toml"

HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)

die() { printf 'step-%s: ERROR: %s\n' "$STEP" "$*" >&2; exit 1; }

# `crates/uv/Cargo.toml` carries exactly one top-level `version = "..."` key,
# inside [package], at every one of the fifteen anchor tags (verified tag by
# tag); the workspace root Cargo.toml never declares [workspace.package].version
# in this range.
read_version() {
  local f="$APP/$VERSION_FILE"
  [ -f "$f" ] || die "$VERSION_FILE not found under $APP"
  awk -F'"' '/^version *=/ { print $2; exit }' "$f"
}

# ---- 0. required tools ------------------------------------------------------
for _t in sha256sum gzip tar awk; do
  command -v "$_t" >/dev/null 2>&1 \
    || die "required tool '$_t' is not on PATH"
done

# ---- 1. verify the payload BEFORE touching anything -------------------------
[ -f "$HERE/SHA256SUMS" ] || die "missing $HERE/SHA256SUMS"
[ -f "$HERE/$PAYLOAD" ]   || die "missing payload $HERE/$PAYLOAD"
( cd -- "$HERE" && sha256sum --check --strict --status SHA256SUMS ) \
  || die "payload checksum verification failed against $HERE/SHA256SUMS"

# ---- 2. populate /app -------------------------------------------------------
mkdir -p -- "$APP" || die "cannot create $APP"
[ -w "$APP" ] || die "$APP is not writable"

tar -C "$APP" -xpzf "$HERE/$PAYLOAD" --no-same-owner \
  || die "failed to unpack $PAYLOAD into $APP"

# ---- 3. assert the resulting declared version -------------------------------
GOT=$(read_version)
[ -n "$GOT" ] || die "could not read a version from $APP/$VERSION_FILE"
[ "$GOT" = "$TARGET_VERSION" ] \
  || die "post-condition failed: $VERSION_FILE declares '$GOT', expected '$TARGET_VERSION'"

printf 'step-%s: OK - %s now declares uv %s\n' "$STEP" "$VERSION_FILE" "$GOT"
