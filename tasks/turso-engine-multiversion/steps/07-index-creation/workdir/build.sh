#!/usr/bin/env bash
# Build the shell and install it where the verifier looks for it.
#
# Use this, or do the same two things your own way. What matters is that
# /app/bin/limbo ends up a regular executable file built from this
# workspace.
set -euo pipefail

cd /app
# The graded container runs under Docker's default seccomp profile, which
# denies the io_uring syscalls -- see "The runtime sandbox" in
# TEST_INTERFACE.md. A build whose I/O backend is io_uring by default cannot
# open a database here and fails every group on the proof-of-life check,
# before any behaviour contract is reached. The line below builds whatever
# your workspace's defaults select, so if you turn on -- or inherit -- a
# feature that makes io_uring the compile-time default backend, turn it off
# on this line. Which flag does that is a property of your own Cargo
# manifests, not of the verifier: check what your default features pull in
# rather than copying a flag from anywhere. No behaviour contract in this
# task depends on the backend, so switching it costs you nothing.
cargo build --release --jobs 4 --bin limbo
mkdir -p /app/bin
install -m 0755 "/app/target/release/limbo" "/app/bin/limbo"
test -x "/app/bin/limbo"
echo "installed /app/bin/limbo"
