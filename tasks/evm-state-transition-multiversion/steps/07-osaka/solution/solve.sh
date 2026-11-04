#!/usr/bin/env bash
# Oracle for step 07: carry `$APP` from go-ethereum 1.15.0 to 1.16.2.
#
# The payload beside this script is the release-to-release transition, shipped
# as whole files plus an explicit deletion list rather than as a unified diff:
# the tree carries binary test fixtures a context diff drops, and a diff can
# half-apply against an edited tree, which would make a broken Oracle run look
# like a clean one.  Nothing here clones, downloads, or fetches.
set -euo pipefail

SOLUTION_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP="${HARBOR_APP_DIR:-/app}"
PREV_VERSION="1.15.0"
TARGET_VERSION="1.16.2"
STEP_PATCH=""

fail() { echo "ORACLE FAILED: $*" >&2; exit 1; }

# A missing tool has to name itself.  An Oracle that exits 127 applies nothing
# and the run still grades cleanly, so a crashed Oracle reads exactly like an
# idle agent -- the two produce the same number.
for tool in tar gzip sha256sum python3 go; do
    command -v "$tool" >/dev/null 2>&1 \
        || { echo "ORACLE PREFLIGHT FAILED: '$tool' is not installed" >&2; exit 127; }
done
[ -d "$APP" ] || fail "no workspace at $APP"

# Verify the payload before touching the workspace.
( cd "$SOLUTION_DIR" && sha256sum -c SHA256SUMS ) >/dev/null \
    || fail "payload checksum mismatch under $SOLUTION_DIR"

geth_version() {
    python3 - "$APP" <<'PYEOF'
import os, re, sys
app = sys.argv[1]
# The constants moved from params/ to version/ and lost their "Version" prefix
# at v1.15.0, so both spellings have to be understood.
for rel, keys in (("params/version.go", ("VersionMajor", "VersionMinor", "VersionPatch")),
                  ("version/version.go", ("Major", "Minor", "Patch"))):
    p = os.path.join(app, rel)
    if not os.path.exists(p):
        continue
    s = open(p).read()
    got = []
    for k in keys:
        m = re.search(r"\b%s\b\s*=\s*(\d+)" % k, s)
        if not m:
            break
        got.append(m.group(1))
    if len(got) == 3:
        print(".".join(got))
        break
else:
    print("unknown")
PYEOF
}

# The arriving version is reported, not enforced: the step before this one is
# an agent's work in a real trial and may legitimately not look like any
# release.  What must hold is the state this script leaves behind, and that is
# asserted at the end.
arriving="$(geth_version)"
[ "$arriving" = "$PREV_VERSION" ] \
    || echo "ORACLE NOTE: workspace arrived as go-ethereum $arriving, expected $PREV_VERSION" >&2

tar -C "$APP" -xzf "$SOLUTION_DIR/bundle.tar.gz" || fail "could not unpack the transition"

while IFS= read -r rel; do
    [ -n "$rel" ] || continue
    case "$rel" in
        /*|*..*) fail "refusing a deletion path that escapes the workspace: $rel" ;;
    esac
    rm -f "$APP/$rel"
done < "$SOLUTION_DIR/DELETED.txt"

# The module replacement is the workspace's, not upstream's, so the transition
# does not carry it and it is restored here.  go-ethereum dropped the memsize
# dependency at v1.15.0; after that there is nothing to replace.
if grep -q 'fjl/memsize' "$APP/go.mod" 2>/dev/null && [ -d "$APP/third_party/memsize-stub" ]; then
    ( cd "$APP" && go mod edit -replace github.com/fjl/memsize=./third_party/memsize-stub ) \
        || fail "could not restore the memsize replacement"
fi

if [ -n "$STEP_PATCH" ]; then
    python3 "$SOLUTION_DIR/$STEP_PATCH" "$APP" || fail "$STEP_PATCH did not apply"
fi

# Assert what this script leaves behind.
got="$(geth_version)"
[ "$got" = "$TARGET_VERSION" ] \
    || fail "expected go-ethereum $TARGET_VERSION after the transition, found $got"
for p in cmd/evm/main.go cmd/evm/internal/t8ntool go.mod go.sum build.sh TEST_INTERFACE.md; do
    [ -e "$APP/$p" ] || fail "$p is missing after the transition"
done

# build/ is excluded from the cumulative artifact, but a stale binary left in
# the agent container would otherwise be the thing a later check finds.
rm -rf "$APP/build"

echo "ORACLE: $APP is go-ethereum $TARGET_VERSION (step 07)"
