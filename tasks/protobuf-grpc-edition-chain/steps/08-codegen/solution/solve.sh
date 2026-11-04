#!/bin/sh
# Reference solution for step 08-codegen.
#
# The tree beside this script is the reference implementation for this step.
# It is copied into /app and built exactly the way a candidate's submission
# is: by running `make` with the toolchain the environment provides.
#
# The tree carries no bin/ and no object files, so `make` always builds from
# source.
set -e
cp -a "$(dirname "$0")/tree/." /app/
cd /app
make
./bin/protoc --version
test -x ./bin/conformance
test -x ./bin/protoc-gen-py
