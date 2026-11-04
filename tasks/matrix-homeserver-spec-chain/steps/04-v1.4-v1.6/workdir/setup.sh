#!/usr/bin/env bash
# The specification releases are reference material, not part of the workspace:
# they are read-only, they are never rebuilt, and a session should not be able
# to lose them.  They live at /spec/<release>/, and each step's own release is
# merged in here, alongside the ones earlier steps left.
#
# This runs as root at the start of every step, so it must not be able to
# fail on anything an earlier session left behind: whatever is in the way is
# replaced, and it always exits 0.
set -u
[ -d /spec ] || rm -rf /spec 2>/dev/null || true
mkdir -p /spec 2>/dev/null || true
if [ -d /app/SPEC ]; then
    for rel in /app/SPEC/*; do
        [ -d "$rel" ] || continue
        name=$(basename "$rel")
        rm -rf "/spec/$name" 2>/dev/null || true
        mv "$rel" "/spec/$name" 2>/dev/null || cp -a "$rel" "/spec/$name" 2>/dev/null || true
    done
    rmdir /app/SPEC 2>/dev/null || true
fi
exit 0
