#!/usr/bin/env bash
# Golden solution for step 04 (PHP 7.2): install the reference
# interpreter sources for this release over the workspace and build them.
set -euo pipefail
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/src"
cd /app
# drop the previous milestone's engine sources so removed files do not linger
rm -rf /app/Zend /app/main /app/ext /app/sapi /app/Makefile
cp -a "$SRC"/. /app/
make -C /app >/dev/null
test -x /app/sapi/cli/php
/app/sapi/cli/php -r 'echo PHP_VERSION, "\n";'
