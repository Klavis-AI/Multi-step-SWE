#!/usr/bin/env bash
# Reference implementation for this milestone.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p /app/src
cp -f "$HERE"/src/*.c "$HERE"/src/*.h /app/src/
cp -f "$HERE"/Makefile /app/Makefile
sed -i 's/^PGVC_MAJOR ?= .*/PGVC_MAJOR ?= 18/' /app/Makefile
rm -f /app/src/*.o /app/src/*.d /app/.pgvc_major
make -C /app -j4
# Leave the workspace source-only: the build products are rebuilt by whoever
# needs them, and a binary file left here does not survive the archive that
# carries the workspace to the next step.
make -C /app clean >/dev/null 2>&1 || rm -f /app/src/*.o /app/src/*.d
rm -rf /app/bin
cat > /app/NOTES.md <<'EOF'
PostgreSQL-compatible server in C11. `make` builds bin/postgres and bin/initdb.
Behaviour is gated on PGVC_MAJOR (set in the Makefile); PGVC_VN in src/pgvc.h is
the ordered form used by the version gates.
EOF
