#!/usr/bin/env bash
# Warm GOMODCACHE from the file:// mirror at image-build time.
#
# Third-party dependency caches are warm; project code is always built from
# clean. Every module published in the mirror is downloaded once here so no
# step pays for it.
set -euo pipefail

MIRROR=/opt/gomodmirror
SCRATCH=$(mktemp -d)
trap 'rm -rf "$SCRATCH"' EXIT

cd "$SCRATCH"
{
    echo "module warmup"
    echo
    echo "go 1.17"
    echo
    echo "require ("
    cut -f1,2 "$MIRROR/MIRROR-INDEX.txt" | while IFS=$'\t' read -r mod ver; do
        printf '\t%s %s\n' "$mod" "$ver"
    done
    echo ")"
} > go.mod

GOFLAGS=-mod=mod go mod download all
echo "warmed $(cut -f1 "$MIRROR/MIRROR-INDEX.txt" | wc -l) modules into $(go env GOMODCACHE)"
