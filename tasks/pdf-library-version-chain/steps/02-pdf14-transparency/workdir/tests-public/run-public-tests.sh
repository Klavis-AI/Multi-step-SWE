#!/usr/bin/env bash
# Public self-check for the PDF utilities in /app.
#
# This is a sample corpus, with its expected answers taken from the same real
# reference tools. It covers only part of the milestone: passing it does not mean the
# milestone is met, but a utility that fails it has not met the milestone either.
#
#   bash /app/tests-public/run-public-tests.sh            # everything
#   bash /app/tests-public/run-public-tests.sh text info  # named groups only
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${BUILD:-/app/build}"

if [ ! -x "$BUILD/pdfinfo" ]; then
    echo "no utilities at $BUILD -- run 'make' in /app first" >&2
    exit 2
fi

TESTS="$HERE" CORPUS_DIR="$HERE/corpus" CAND_BUILD="$BUILD" \
LOG="${LOG:-/tmp/public-tests}" CASE_WORKERS="${CASE_WORKERS:-$(nproc)}" \
CASE_DEADLINE="${CASE_DEADLINE:-3600}" PUBLIC_GROUPS="$*" \
    python3 "$HERE/run_public.py"
