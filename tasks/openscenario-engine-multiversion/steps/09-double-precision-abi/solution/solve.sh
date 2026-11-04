#!/usr/bin/env bash
set -euo pipefail

# Oracle for step 09: apply the exact upstream release-to-release
# transition of esmini from v2.59.0 to v3.0.4 over the cumulative /app tree.
# Nothing is cloned, downloaded or fetched at evaluation time.
#
#   upstream repository  https://github.com/esmini/esmini
#   upstream tag         v3.0.4
#   licence              MPL-2.0 (LICENSE travels inside the patch)
#
# The patch was produced by `git diff --no-index` between two `git archive`
# exports, so its headers read a/trees/<tag>/... and it applies at -p3. See
# misc/openscenario-engine-multiversion/ORACLE.md.
#
# Task-owned files in the workspace -- TEST_INTERFACE.md, WORKSPACE_HANDOFF.md,
# PROFILE.md, timer.sh -- are not upstream paths, so no hunk touches them.

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

( cd "$solution_dir" && sha256sum -c SHA256SUMS )

cd "$app_dir"

# Build output must not be in the tree when a source patch lands on it, and it
# is never part of the transition.
rm -rf -- bin build cmake_build

# GIT_CEILING_DIRECTORIES stops Git's repository discovery from walking above
# the workspace, so the patch paths stay relative to /app even when /app sits
# inside an unrelated checkout.
GIT_CEILING_DIRECTORIES=$(dirname -- "$app_dir") \
    git apply --binary --whitespace=nowarn -p3 "$solution_dir/upgrade.patch"

# Post-conditions: the product is still the product, and the task-owned
# documents survived the transition.
test -f "$app_dir/CMakeLists.txt"
test -d "$app_dir/EnvironmentSimulator"
test -d "$app_dir/resources"
test -f "$app_dir/LICENSE"

test -f "$app_dir/TEST_INTERFACE.md"
test -f "$app_dir/WORKSPACE_HANDOFF.md"

printf 'applied esmini v2.59.0 -> v3.0.4 (release 3.0.4)\n'
