#!/usr/bin/env bash
# Build entry point.  The verifier runs exactly this script and then expects
# the executable /app/build/t8n; it does not know or care how you produce it.
#
# You may change what is below -- restructure the tree, rename packages, switch
# to a Makefile -- as long as this script still produces /app/build/t8n.
#
# It must succeed with GOPROXY=off against the prewarmed module cache: the
# verifier builds offline first and only falls back to the network if that
# fails.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
go build -p 4 -o build/t8n ./cmd/evm
