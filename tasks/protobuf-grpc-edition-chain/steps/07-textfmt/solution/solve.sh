#!/bin/sh
# Reference solution for step 07-textfmt.
#
# The tree beside this script is the reference implementation for this step.
# It is copied into /app and built exactly the way a candidate's submission
# is: by running `make` with the toolchain the environment provides.
set -e
cp -a "$(dirname "$0")/tree/." /app/
cd /app
make
./bin/protoc --version
test -x ./bin/conformance
