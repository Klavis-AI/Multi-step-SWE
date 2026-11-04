#!/usr/bin/env bash
# Oracle for step 01 (01-baseline-engine): upstream turso at v0.0.13.
#
# Deterministic, local, and safe to rerun in a fresh cumulative worktree. It
# never clones, downloads, or invokes anything from the network.
set -euo pipefail

solution_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cd "$solution_dir"
sha256sum -c SHA256SUMS

cd /app
# The baseline tree replaces the starter skeleton's source directories. The
# workspace's author-owned documents live outside them and are left alone.
for d in core cli macros extensions sqlite3 vendored; do
    rm -rf "/app/$d"
done
rm -f /app/Cargo.toml /app/Cargo.lock
tar -xzf "$solution_dir/baseline-v0.0.13.tar.gz" -C /app

test -f /app/Cargo.toml || { echo "no workspace manifest after applying payload" >&2; exit 1; }

export CARGO_TERM_COLOR=never CARGO_NET_RETRY=10 RUSTUP_TOOLCHAIN=1.88.0
# The CLI crate's only default feature is `io_uring`, and from v0.0.15 to
# v0.0.18 its compile-time default I/O backend is io_uring whenever that feature
# is on. The grading container runs under Docker's default seccomp profile,
# which denies `io_uring_setup`, so such a build cannot open a database at all.
# Turning the feature off selects the syscall backend and changes nothing else:
# it is the crate's only default, and no graded behaviour depends on the backend.
cargo build --release --jobs 4 --no-default-features -p limbo --bin limbo

mkdir -p /app/bin
install -m 0755 "/app/target/release/limbo" "/app/bin/limbo"


# Assert the result rather than assuming the build did what was asked.
test -x "/app/bin/limbo"
BINPATH="/app/bin/limbo"
MODE="raw"
printf 'SELECT 1;\n' | "$BINPATH" -m $MODE -q /tmp/oracle-smoke-01.db | grep -qx 1
rm -f /tmp/oracle-smoke-01.db /tmp/oracle-smoke-01.db-wal

echo "oracle step 01 (v0.0.13) installed /app/bin/limbo"
