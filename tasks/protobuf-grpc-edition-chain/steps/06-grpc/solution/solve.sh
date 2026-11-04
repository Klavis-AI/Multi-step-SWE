#!/bin/sh
# Reference solution for step 06-grpc.
#
# The tree beside this script is the reference implementation for this step.
# It is copied into /app and built exactly the way a candidate's submission
# is: by running `make` with the toolchain the environment provides.
set -e
cp -a "$(dirname "$0")/tree/." /app/
cd /app
make
make grpc
./bin/protoc --version
test -x ./bin/conformance
test -x ./bin/grpc_server
test -x ./bin/grpc_client
