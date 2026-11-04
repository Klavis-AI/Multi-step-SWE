#!/usr/bin/env bash
set -euo pipefail

# Oracle for milestone 1: install earth-mover/icechunk at tag icechunk-v0.1.0-alpha.1 over the
# seeded scaffold. Nothing is cloned, downloaded or fetched at evaluation time;
# the payload beside this script is the whole input.
#
#   upstream repository  https://github.com/earth-mover/icechunk
#   upstream tag         icechunk-v0.1.0-alpha.1  (commit e20f7e8663eedb737c712cdbcceeec21cc3b1b2e)
#   licence              Apache-2.0
#   payload              the full upstream worktree at that tag, unmodified

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Verify the payload before touching the workspace.
( cd "$solution_dir" && sha256sum -c SHA256SUMS )

cd "$app_dir"

# Replace the scaffold's implementation and manifests. Everything else the
# seeded workspace supplied - icechunk-spec/, TEST_INTERFACE.md,
# WORKSPACE_HANDOFF.md, timer.sh - is preserved.
rm -rf -- icechunk target
rm -f -- Cargo.toml Cargo.lock
tar -xzf "$solution_dir/icechunk-alpha.1-source.tar.gz" -C "$app_dir"

# Assert the result rather than trusting the extraction.
test -f "$app_dir/Cargo.toml"
test -f "$app_dir/Cargo.lock"
test -f "$app_dir/icechunk/Cargo.toml"
test -f "$app_dir/icechunk/src/lib.rs"
test -f "$app_dir/icechunk/src/format/mod.rs"
test -f "$app_dir/spec/icechunk-spec.md"
grep -q '^name = "icechunk"' "$app_dir/icechunk/Cargo.toml"
grep -q '^version = "0.1.0-alpha.1"' "$app_dir/icechunk/Cargo.toml"

# The seeded documentation must survive the transition.
test -f "$app_dir/TEST_INTERFACE.md"
test -f "$app_dir/WORKSPACE_HANDOFF.md"
test -d "$app_dir/icechunk-spec"

printf 'installed earth-mover/icechunk at icechunk-v0.1.0-alpha.1 (e20f7e86)\n'
