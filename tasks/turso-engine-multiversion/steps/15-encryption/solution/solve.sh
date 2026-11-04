#!/usr/bin/env bash
# Oracle for step 15 (15-encryption): upstream turso at v0.1.5.
#
# Deterministic, local, and safe to rerun in a fresh cumulative worktree. It
# never clones, downloads, or invokes anything from the network.
set -euo pipefail

solution_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cd "$solution_dir"
sha256sum -c SHA256SUMS

cd /app
git apply --binary --whitespace=nowarn -p1 "$solution_dir/release-v0.1.4-to-v0.1.5.patch"

test -f /app/Cargo.toml || { echo "no workspace manifest after applying payload" >&2; exit 1; }

export CARGO_TERM_COLOR=never CARGO_NET_RETRY=10 RUSTUP_TOOLCHAIN=1.88.0
# The CLI crate's only default feature is `io_uring`, and from v0.0.15 to
# v0.0.18 its compile-time default I/O backend is io_uring whenever that feature
# is on. The grading container runs under Docker's default seccomp profile,
# which denies `io_uring_setup`, so such a build cannot open a database at all.
# Turning the feature off selects the syscall backend and changes nothing else:
# it is the crate's only default, and no graded behaviour depends on the backend.
cargo build --release --jobs 4 --no-default-features -p turso_cli --features turso_core/encryption --bin tursodb

mkdir -p /app/bin
install -m 0755 "/app/target/release/tursodb" "/app/bin/tursodb"


# Assert the result rather than assuming the build did what was asked.
test -x "/app/bin/tursodb"
BINPATH="/app/bin/tursodb"
MODE="list"
printf 'SELECT 1;\n' | "$BINPATH" -m $MODE -q /tmp/oracle-smoke-15.db | grep -qx 1
rm -f /tmp/oracle-smoke-15.db /tmp/oracle-smoke-15.db-wal

# The encryption feature is compiled in, so prove it: write a row through an
# engaged cipher-and-key pair, reopen the file in a second process with the
# same pair, and require the row back. A build without the feature panics on
# the first encrypted page write instead, which fails this check loudly here
# rather than silently at verification time.
encdir="$(mktemp -d)"
(
    cd "$encdir"
    "$BINPATH" -m "$MODE" -q enc.db > /dev/null <<'ENCSQL'
PRAGMA cipher = 'aegis256';
PRAGMA hexkey = '000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f';
CREATE TABLE s(v TEXT);
INSERT INTO s VALUES('encrypted');
SELECT v FROM s;
ENCSQL
    "$BINPATH" -m "$MODE" -q enc.db <<'ENCSQL' | grep -qx encrypted
PRAGMA cipher = 'aegis256';
PRAGMA hexkey = '000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f';
SELECT v FROM s;
ENCSQL
)
rm -rf "$encdir"
echo "oracle step 15 (v0.1.5) installed /app/bin/tursodb"
