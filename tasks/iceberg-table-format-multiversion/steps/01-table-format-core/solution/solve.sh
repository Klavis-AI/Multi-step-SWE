#!/usr/bin/env bash
set -euo pipefail

# Oracle for milestone 1: install apache/iceberg-rust `crates/iceberg` at tag
# 0.2.0 over the seeded scaffold. Nothing is cloned, downloaded or fetched at
# evaluation time; the payload beside this script is the whole input.
#
#   upstream repository  https://github.com/apache/iceberg-rust
#   upstream tag         0.2.0  (commit adef9bda28ad4c00bd837e0299be230c4789d90c)
#   licence              Apache-2.0 (LICENSE and NOTICE ship inside the archive)
#   task-owned files     Cargo.toml (de-workspaced), Cargo.lock (upstream
#                        committed none at this tag)

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Verify the payload before touching the workspace.
( cd "$solution_dir" && sha256sum -c SHA256SUMS )

cd "$app_dir"

# Replace the scaffold's implementation. Everything else the seeded workspace
# supplied - iceberg-spec/, TEST_INTERFACE.md, WORKSPACE_HANDOFF.md, timer.sh -
# is preserved.
rm -rf -- src target
tar -xzf "$solution_dir/iceberg-0.2.0-source.tar.gz" -C "$app_dir"

# Assert the result rather than trusting the extraction.
test -f "$app_dir/Cargo.toml"
test -f "$app_dir/Cargo.lock"
test -f "$app_dir/LICENSE"
test -f "$app_dir/NOTICE"
test -f "$app_dir/src/lib.rs"
test -f "$app_dir/src/spec/table_metadata.rs"
grep -q '^name = "iceberg"' "$app_dir/Cargo.toml"
grep -q '^version = "0.2.0"' "$app_dir/Cargo.toml"

# The seeded documentation must survive the transition.
test -f "$app_dir/TEST_INTERFACE.md"
test -f "$app_dir/WORKSPACE_HANDOFF.md"
test -d "$app_dir/iceberg-spec"

printf 'installed apache/iceberg-rust crates/iceberg at 0.2.0 (adef9bda)\n'
