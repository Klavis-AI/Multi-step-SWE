#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: apply the exact upstream release-to-release
# transition from apache/iceberg-rust v0.5.1 to v0.6.0 over the cumulative /app tree.
# Nothing is cloned, downloaded or fetched at evaluation time.
#
#   upstream repository  https://github.com/apache/iceberg-rust
#   upstream tag         v0.6.0  (commit dfbab0da0000fe3f16e793415aeef642e529acee)
#   licence              Apache-2.0 (LICENSE and NOTICE reinstalled below)
#   task-owned files     Cargo.toml (de-workspaced, carried in the patch),
#                        Cargo.lock (installed beside it)

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

( cd "$solution_dir" && sha256sum -c SHA256SUMS )

cd "$app_dir"

# The transition is a plain unified diff over Cargo.toml and src/. --binary
# keeps it byte-exact and needs no Git object database; /app is not a Git
# repository.
rm -rf -- target
# GIT_CEILING_DIRECTORIES stops Git's repository discovery from walking above
# the workspace, so the patch paths stay relative to /app even when /app sits
# inside an unrelated checkout.
GIT_CEILING_DIRECTORIES=$(dirname -- "$app_dir") \
    git apply --binary --whitespace=nowarn -p1 "$solution_dir/release-v0.5.1-to-v0.6.0.patch"

# Upstream committed no usable single-crate lockfile at this tag, so the exact
# task-owned resolution is installed here.
cp -- "$solution_dir/Cargo.lock" "$app_dir/Cargo.lock"
cp -- "$solution_dir/LICENSE" "$app_dir/LICENSE"
cp -- "$solution_dir/NOTICE" "$app_dir/NOTICE"

test -f "$app_dir/Cargo.toml"
test -f "$app_dir/src/lib.rs"
grep -q '^name = "iceberg"' "$app_dir/Cargo.toml"
grep -q '^version = "0.6.0"' "$app_dir/Cargo.toml"

test -f "$app_dir/TEST_INTERFACE.md"
test -f "$app_dir/WORKSPACE_HANDOFF.md"
test -d "$app_dir/iceberg-spec"

printf 'applied apache/iceberg-rust v0.5.1 -> v0.6.0 (dfbab0da0000fe3f16e793415aeef642e529acee)\n'
