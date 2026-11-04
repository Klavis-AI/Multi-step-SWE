#!/usr/bin/env bash
# Oracle for step 04-copc: upstream PDAL 2.3.0 -> 2.4.0.
#
# Applies bytes that ship inside this package. It never clones, downloads, or
# reaches the network, and it runs no build: the verifier builds /app itself.
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$here"

# 1. The payload must be the payload this package was built with.
sha256sum -c SHA256SUMS

# 2. Apply the exact upstream transition. --binary carries binary files;
#    --whitespace=nowarn keeps upstream's own whitespace verbatim. A failure
#    here means /app is not 2.3.0, which is the one thing worth failing loudly
#    about: a partially-applied Oracle would score as an ordinary bad run.
cd /app
git apply --binary --whitespace=nowarn "$here/upgrade.patch"

# 3. Assert the result. Upstream declares its version in the top-level
#    CMakeLists.txt from 2.1.0 onward.
grep -q 'project(PDAL VERSION 2.4.0 ' /app/CMakeLists.txt \
  || { echo "solve.sh: /app is not PDAL 2.4.0 after applying the patch" >&2; exit 1; }

echo "solve.sh: /app is now upstream PDAL 2.4.0"
