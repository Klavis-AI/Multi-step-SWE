#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: reconstruct the exact published Haivision/srt
# release v1.4.2 over the cumulative /app tree. Nothing is cloned, downloaded or
# fetched; the payload beside this script is the whole of it.
#
#   upstream release   srt v1.4.2 (50b7af0, 2020-09-25)
#   transition         diff of the release trees v1.4.0 -> v1.4.2
#   licence            MPL-2.0 (LICENSE)

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

( cd "$solution_dir" && sha256sum -c SHA256SUMS )

cd "$app_dir"

# Build products are not part of the transition and would collide with it.
rm -rf -- build _build

# The transition is a plain unified diff of the two release trees, with the
# comparison's leading component already stripped, hence -p1. --binary keeps it
# byte-exact and needs no Git object database: /app is not a Git repository,
# and GIT_CEILING_DIRECTORIES stops git from finding one above it.
GIT_CEILING_DIRECTORIES="$app_dir" git apply -p1 --binary --whitespace=nowarn \
    "$solution_dir/release-v1.4.0-to-v1.4.2.patch"

# The milestone is this release and not merely "a tree that applied cleanly".
grep -qE '^set *\( *SRT_VERSION +1.4.2 *\)' "$app_dir/CMakeLists.txt"
test -f "$app_dir/srtcore/srt.h"

# Every upstream-owned file, byte for byte. A fuzzy or partial apply stops here
# instead of surfacing three milestones later as a capability gap. Paths the
# task seeds into /app are absent from the manifest and are neither checked nor
# disturbed.
( cd "$app_dir" && sha256sum --quiet -c "$solution_dir/TREE_MANIFEST.sha256" )

# The seeded documentation must survive the transition.
test -f "$app_dir/PROFILE.md"
test -f "$app_dir/TEST_INTERFACE.md"
test -f "$app_dir/WORKSPACE_HANDOFF.md"
test -d "$app_dir/srt-docs"

printf 'applied Haivision/srt %s -> %s (%s upstream files verified)\n' \
    'v1.4.0' 'v1.4.2' '234'
