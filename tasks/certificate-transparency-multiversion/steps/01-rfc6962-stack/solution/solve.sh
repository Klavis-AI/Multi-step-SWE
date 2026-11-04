#!/usr/bin/env bash
set -euo pipefail

# Oracle for step 01 (01-rfc6962-stack): install the product at upstream release v1.0 over
# the seeded scaffold. Nothing is cloned, downloaded or fetched; the payload
# beside this script is the whole input.
#
#   upstream release   certificate-transparency-go v1.0, 2017-09-11
#   commit             abb79e468b6f3bbd48d1ab0c9e68febf80d52c4d
#   licence            Apache-2.0 (LICENSE ships inside the payload)
#
# Three paths are deliberately NOT in the payload because the benchmark owns
# them rather than the candidate (SCOPE-BUILD in TEST_INTERFACE.md):
#   vendor/   upstream's 2017-era partial vendor tree, unusable offline today
#   go.mod    the pinned build manifest, delivered by the workspace overlay
#   go.sum    ditto
# Everything else the tag ships - sources, upstream's own tests, testdata,
# protos, scripts, notices - is in the payload.

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Verify the payload before touching the workspace.
( cd "$solution_dir" && sha256sum -c SHA256SUMS )

cd "$app_dir"

tar -xzf "$solution_dir/payload.tar.gz" -C "$app_dir"

# Assert the result rather than trusting the extraction. These paths were
# chosen from the upstream tree for this exact transition.
test -f "$app_dir/serialization.go"
test -f "$app_dir/signatures.go"
test -f "$app_dir/types.go"

# The benchmark-owned build manifest and the seeded documentation must survive.
test -f "$app_dir/go.mod"
test -f "$app_dir/go.sum"
test -f "$app_dir/TEST_INTERFACE.md"
test -f "$app_dir/WORKSPACE_HANDOFF.md"
test -f "$app_dir/timer.sh"
test -d "$app_dir/ct-spec"

printf 'installed certificate-transparency-go v1.0 (commit abb79e468b6f3bbd48d1ab0c9e68febf80d52c4d)\n'
