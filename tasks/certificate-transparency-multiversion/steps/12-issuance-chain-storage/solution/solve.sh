#!/usr/bin/env bash
set -euo pipefail

# Oracle for step 12 (12-issuance-chain-storage): apply the exact upstream release-to-release
# transition v1.1.4 -> v1.2.0 over the cumulative /app tree. Nothing is cloned,
# downloaded or fetched; the payload beside this script is the whole input.
#
#   upstream release   certificate-transparency-go v1.2.0, 2024-05-28
#   commit             0c9c98f339b9304a5ffbff496c2b032883319607
#   licence            Apache-2.0
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

# A plain unified diff over the product tree. --binary keeps the testdata
# blobs byte-exact and needs no object database; the workspace is not a Git
# repository, and GIT_CEILING_DIRECTORIES stops git walking out of /app if one
# ever appears above it. git apply is atomic: if /app is not the previous
# step's Oracle tree the patch is refused whole rather than half-applied.
GIT_CEILING_DIRECTORIES=$(dirname -- "$app_dir") \
    git apply --binary --whitespace=nowarn -p1 "$solution_dir/transition.patch"

# Assert the result rather than trusting the extraction. These paths were
# chosen from the upstream tree for this exact transition.
test -f "$app_dir/x509/root_aix.go"
test -f "$app_dir/x509/root_wasip1.go"
test -f "$app_dir/x509/root_zos.go"
test ! -e "$app_dir/loglist2"   # removed by this release

# The benchmark-owned build manifest and the seeded documentation must survive.
test -f "$app_dir/go.mod"
test -f "$app_dir/go.sum"
test -f "$app_dir/TEST_INTERFACE.md"
test -f "$app_dir/WORKSPACE_HANDOFF.md"
test -f "$app_dir/timer.sh"
test -d "$app_dir/ct-spec"

printf 'installed certificate-transparency-go v1.2.0 (commit 0c9c98f339b9304a5ffbff496c2b032883319607)\n'
