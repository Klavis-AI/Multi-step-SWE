#!/usr/bin/env bash
# Runs the example conformance programs through ./lua and reports how many
# produce exactly the reference implementation's stdout, exit status and first
# line of stderr.  The examples are a small representative sample, so a clean
# result is necessary but not sufficient.
set -u
cd "$(dirname "$0")/.." || exit 1
ROOT=$PWD
if [ ! -x "$ROOT/lua" ]; then
    echo "no ./lua — run make first"
    exit 1
fi
python3 "$ROOT/tests/conformance/check.py" "$ROOT" "$ROOT/tests/conformance" "$@"
