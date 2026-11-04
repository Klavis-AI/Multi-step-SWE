#!/usr/bin/env bash
# Reference solution for step 1: the implementation as it stands at the end of
# this milestone.
#
# Harbor never resets the container between steps, so /app still holds the
# previous milestone's tree.  This MIRRORS that tree instead of copying over
# it: a milestone that renames, splits or drops a source file must not leave
# the old one behind for the next `make` to pick up.
# Everything the step's own workdir supplies is preserved.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
mkdir -p /app
python3 - "$here/impl" <<'PY'
import os, shutil, sys
supplied = {"SPEC", "WORKSPACE_HANDOFF.md", "timer.sh"}
keep = supplied | set(os.listdir(sys.argv[1]))
for name in os.listdir("/app"):
    if name in keep:
        continue
    p = os.path.join("/app", name)
    print("mirror: dropping stale %s" % p)
    shutil.rmtree(p, ignore_errors=True) if os.path.isdir(p) else os.remove(p)
for name in os.listdir(sys.argv[1]):
    p = os.path.join("/app", name)
    if os.path.isdir(p) and not os.path.islink(p):
        shutil.rmtree(p, ignore_errors=True)
PY
cp -a "$here/impl/." /app/
cd /app
make
./out/bin/git version
