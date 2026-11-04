#!/usr/bin/env bash
# Golden solution: install the reference C implementation and build it.
set -euo pipefail
mkdir -p /app/evm
cp -a "$(dirname "$0")"/evm/. /app/evm/
cd /app/evm
make -s
/app/evm/evm --version
