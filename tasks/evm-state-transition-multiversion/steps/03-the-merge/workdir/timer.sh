#!/usr/bin/env bash
# Build timer.  Prints elapsed wall-clock seconds for the command it wraps, so
# you can see whether a change made the build slower.  Purely informational: the
# verifier does not read its output.
set -u
start=$(date +%s)
"$@"
rc=$?
end=$(date +%s)
echo "[timer] $* -> rc=$rc in $((end - start))s" >&2
exit $rc
