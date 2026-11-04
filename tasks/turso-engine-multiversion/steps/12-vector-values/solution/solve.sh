#!/usr/bin/env bash
# Oracle for step 12 (12-vector-values): upstream turso at v0.1.2.
#
# Deterministic, local, and safe to rerun in a fresh cumulative worktree. It
# never clones, downloads, or invokes anything from the network.
set -euo pipefail

solution_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cd "$solution_dir"
sha256sum -c SHA256SUMS

cd /app
git apply --binary --whitespace=nowarn -p1 "$solution_dir/release-v0.1.0-to-v0.1.2.patch"

test -f /app/Cargo.toml || { echo "no workspace manifest after applying payload" >&2; exit 1; }

export CARGO_TERM_COLOR=never CARGO_NET_RETRY=10 RUSTUP_TOOLCHAIN=1.88.0
# The CLI crate's only default feature is `io_uring`, and from v0.0.15 to
# v0.0.18 its compile-time default I/O backend is io_uring whenever that feature
# is on. The grading container runs under Docker's default seccomp profile,
# which denies `io_uring_setup`, so such a build cannot open a database at all.
# Turning the feature off selects the syscall backend and changes nothing else:
# it is the crate's only default, and no graded behaviour depends on the backend.
cargo build --release --jobs 4 --no-default-features -p turso_cli --bin tursodb

mkdir -p /app/bin
install -m 0755 "/app/target/release/tursodb" "/app/bin/tursodb"


# Assert the result rather than assuming the build did what was asked.
test -x "/app/bin/tursodb"
BINPATH="/app/bin/tursodb"
MODE="list"
printf 'SELECT 1;\n' | "$BINPATH" -m $MODE -q /tmp/oracle-smoke-12.db | grep -qx 1
rm -f /tmp/oracle-smoke-12.db /tmp/oracle-smoke-12.db-wal

echo "oracle step 12 (v0.1.2) installed /app/bin/tursodb"
