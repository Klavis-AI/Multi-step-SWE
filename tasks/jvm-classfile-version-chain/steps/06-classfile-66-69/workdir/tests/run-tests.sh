#!/usr/bin/env bash
# Runs the sample conformance corpus in this directory against the JVM built in
# /app/jvm.  Each test directory holds class files, the exact `java` arguments in
# `cmd`, and the behaviour recorded from a reference JDK in `expected.json`.
#
#   bash /app/tests/run-tests.sh            # every sample test
#   bash /app/tests/run-tests.sh arith      # only tests whose name contains it
set -u
exec python3 "$(dirname "$0")/run-tests.py" "$@"
