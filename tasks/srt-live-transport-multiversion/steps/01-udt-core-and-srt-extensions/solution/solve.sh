#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: reconstruct the exact published Haivision/srt
# release v1.2.0 over the cumulative /app tree. Nothing is cloned, downloaded or
# fetched; the payload beside this script is the whole of it.
#
#   upstream release   srt v1.2.0 (891a66d, 2017-07-28)
#   transition         reproducible source tarball of v1.2.0
#   licence            LGPL-2.1 (COPYING)

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

( cd "$solution_dir" && sha256sum -c SHA256SUMS )

cd "$app_dir"

# Build products are not part of the transition and would collide with it.
rm -rf -- build _build

# Step 01 seeds the tree, so the payload is a reproducible source tarball
# rather than a diff. Extracted over /app, which already holds the seeded
# documentation; the tarball contains no path that collides with it.
tar -xzf "$solution_dir/srt-v1.2.0-source.tar.gz" -C "$app_dir"

# The milestone is this release and not merely "a tree that applied cleanly".
grep -qE '^set *\( *SRT_VERSION +1.2.0 *\)' "$app_dir/CMakeLists.txt"
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
    '(empty)' 'v1.2.0' '91'
