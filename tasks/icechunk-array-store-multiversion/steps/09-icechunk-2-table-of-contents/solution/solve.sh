#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: apply the exact upstream release-to-release
# transition from earth-mover/icechunk v1.1.21 to v2.0.0 over the cumulative
# /app tree. Nothing is cloned, downloaded or fetched at evaluation time.
#
#   upstream repository  https://github.com/earth-mover/icechunk
#   upstream tag         v2.0.0
#   licence              Apache-2.0
#   payload              `git diff --binary --no-renames v1.1.21 v2.0.0`

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

( cd "$solution_dir" && sha256sum -c SHA256SUMS )

cd "$app_dir"

# The transition is a plain unified diff over the upstream worktree. --binary
# keeps it byte-exact and needs no Git object database; /app is not a Git
# repository. GIT_CEILING_DIRECTORIES stops Git's repository discovery from
# walking above the workspace, so the patch paths stay relative to /app even
# when /app sits inside an unrelated checkout.
rm -rf -- target
GIT_CEILING_DIRECTORIES=$(dirname -- "$app_dir") \
    git apply --binary --whitespace=nowarn -p1 \
        "$solution_dir/release-v1.1.21-to-v2.0.0.patch"

test -f "$app_dir/Cargo.toml"
test -f "$app_dir/Cargo.lock"
test -f "$app_dir/icechunk/Cargo.toml"
test -f "$app_dir/icechunk/src/lib.rs"
grep -q '^name = "icechunk"' "$app_dir/icechunk/Cargo.toml"
grep -qE '^version = ("2.0.0"|\{ workspace = true \})' "$app_dir/icechunk/Cargo.toml"

test -f "$app_dir/TEST_INTERFACE.md"
test -f "$app_dir/WORKSPACE_HANDOFF.md"
test -d "$app_dir/icechunk-spec"

printf 'applied earth-mover/icechunk v1.1.21 -> v2.0.0\n'
