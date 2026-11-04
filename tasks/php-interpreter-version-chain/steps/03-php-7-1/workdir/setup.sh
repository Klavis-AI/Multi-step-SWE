#!/bin/bash
# Runs once when this milestone starts, before the session begins.
#
# A log directory left over from the previous milestone may still be present in this
# container; it is not part of this workspace and nothing here needs it.
# This removes it, then removes itself.
rm -rf /logs/verifier 2>/dev/null || true
# Each milestone's workspace is extracted over /app with `tar xf`, which adds and
# overwrites but never deletes -- so a test php-src DELETED at this release would otherwise
# linger from an earlier milestone's copy of the corpus.  By the last milestone that is 327
# tests, 279 of which the release's own interpreter fails, against an instruction that says
# the corpus is this tag taken verbatim and to make it pass.  Remove exactly those, listed
# by path, so /app/tests/phpt is the tag and nothing else.
REMOVED="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/tests/phpt-removed.txt"
if [ -s "$REMOVED" ]; then
    while IFS= read -r p; do
        [ -n "$p" ] && rm -f "/app/tests/phpt/$p" 2>/dev/null || true
    done < "$REMOVED"
    find /app/tests/phpt -type d -empty -delete 2>/dev/null || true
fi
rm -f "$REMOVED" 2>/dev/null || true
rm -f "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/setup.sh" 2>/dev/null || true
exit 0
