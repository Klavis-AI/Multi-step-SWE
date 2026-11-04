#!/usr/bin/env bash
set -euo pipefail

# Oracle for milestone 06 of ooxml-spreadsheet-engine-multiversion.
#
#   upstream project   qax-os/excelize, BSD-3-Clause
#   checkpoint         v2.2.0
#   payload            patch v2.1.0 -> v2.2.0
#
# The payload beside this script is the whole input: nothing is cloned,
# downloaded or fetched, and no network is touched.  The only edit made to
# upstream source is the package rename recorded in misc/ooxml-spreadsheet-engine-multiversion/ORACLE.md.

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Verify the payload before touching the workspace.
( cd "$solution_dir" && sha256sum -c SHA256SUMS )

cd "$app_dir"

# Apply the release-to-release transition onto the existing cumulative /app.
patch -p1 --batch --forward -d "$app_dir" < "$solution_dir/step-06.patch"

# Restore the pinned manifests: the dependency set is one pinned union for the
# whole ladder, seeded at step 01 and never changed by a transition.
test -f "$app_dir/go.mod"
test -f "$app_dir/go.sum"
test -d "$app_dir/vendor"

# Assert the resulting version rather than trusting the transition.
go_files=$(find "$app_dir" -maxdepth 1 -name '*.go' | wc -l)
[ "$go_files" -eq 47 ] || { echo "expected 47 root Go files, found $go_files" >&2; exit 1; }
digest=$(cd "$app_dir" && find . -maxdepth 1 -name '*.go' -print0 | LC_ALL=C sort -z \
    | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)
[ "$digest" = "a3300c94be4948c1ecf1acd27652a565bf8b4d13593624979572036bbb56eb3e" ] || { echo "tree digest $digest != a3300c94be4948c1ecf1acd27652a565bf8b4d13593624979572036bbb56eb3e" >&2; exit 1; }

# The seeded documentation must survive the transition.
test -f "$app_dir/TEST_INTERFACE.md"
test -f "$app_dir/WORKSPACE_HANDOFF.md"
test -d "$app_dir/specs"

printf 'installed seedcalc milestone 06 (excelize v2.2.0, 47 root Go files)\n'
