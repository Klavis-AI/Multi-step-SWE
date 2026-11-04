#!/usr/bin/env bash
# Oracle solution for step 06 of the uv release chain.
#
#   transition : patch 0.4.20 -> 0.5.0 applied to the existing /app
#   payload    : uv-0.4.20-to-0.5.0.patch.gz
#
# This script is fully offline: it clones nothing, downloads nothing, fetches
# nothing, and invokes no build tool.  It uses only sha256sum, gzip, tar/git
# and shell builtins against the payload sitting next to it on disk.
set -Eeuo pipefail

# Operates on /app.  ORACLE_APP_DIR exists only so the chain can be verified
# against a scratch "/app-like" directory without root; unset (the normal case,
# and the only case in the grading container) this is exactly /app.
APP="${ORACLE_APP_DIR:-/app}"
STEP="06"
TARGET_VERSION="0.5.0"
PAYLOAD="uv-0.4.20-to-0.5.0.patch.gz"
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
for _t in sha256sum gzip git awk mktemp; do
  command -v "$_t" >/dev/null 2>&1 \
    || die "required tool '$_t' is not on PATH"
done

# ---- 1. verify the payload BEFORE touching anything -------------------------
[ -f "$HERE/SHA256SUMS" ] || die "missing $HERE/SHA256SUMS"
[ -f "$HERE/$PAYLOAD" ]   || die "missing payload $HERE/$PAYLOAD"
( cd -- "$HERE" && sha256sum --check --strict --status SHA256SUMS ) \
  || die "payload checksum verification failed against $HERE/SHA256SUMS"

# ---- 2. apply the release-to-release patch to the existing /app -------------
PREV_VERSION="0.4.20"

[ -d "$APP" ] || die "$APP does not exist; step 01 must run first"
[ -w "$APP" ] || die "$APP is not writable"

_have=$(read_version)
[ "$_have" = "$PREV_VERSION" ] \
  || die "$APP declares version '$_have' but this patch applies to '$PREV_VERSION'; run the earlier steps first"

PATCH=$(mktemp "${TMPDIR:-/tmp}/oracle-step-$STEP-XXXXXX.patch")
cleanup() { rm -f -- "$PATCH"; }
trap cleanup EXIT

gzip -dc -- "$HERE/$PAYLOAD" > "$PATCH" || die "failed to decompress $PAYLOAD"

# /app is a plain directory tree, not a git repository.  `git apply` works fine
# there: without --index/--cached it only ever touches the working tree.
#   --binary        : apply GIT binary patches (uv ships binary test fixtures;
#                     GNU patch(1) cannot represent or apply these at all)
#   (--full-index is a git-diff option, not a git-apply one: the patch was
#    generated with it, so its index lines carry complete preimage blob
#    SHA-1s that git apply verifies before writing anything)
#   -p1             : strip the a/ and b/ prefixes
#   --whitespace=nowarn : never rewrite or complain about whitespace; the
#                     postimage must land byte for byte
# GIT_CEILING_DIRECTORIES stops git from discovering a repository *above* /app
# and re-rooting the paths there.
cd -- "$APP" || die "cannot cd to $APP"
export GIT_CEILING_DIRECTORIES="$APP"

git apply --check --binary -p1 --whitespace=nowarn -- "$PATCH" \
  || die "patch does not apply cleanly to $APP (tree is not at $PREV_VERSION, or has been modified)"
git apply --binary -p1 --whitespace=nowarn -- "$PATCH" \
  || die "patch application failed part-way through; $APP is now in an unknown state"

# ---- 3. assert the resulting declared version -------------------------------
GOT=$(read_version)
[ -n "$GOT" ] || die "could not read a version from $APP/$VERSION_FILE"
[ "$GOT" = "$TARGET_VERSION" ] \
  || die "post-condition failed: $VERSION_FILE declares '$GOT', expected '$TARGET_VERSION'"

printf 'step-%s: OK - %s now declares uv %s\n' "$STEP" "$VERSION_FILE" "$GOT"
