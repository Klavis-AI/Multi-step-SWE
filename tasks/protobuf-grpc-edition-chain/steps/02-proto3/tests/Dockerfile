# Verifier image for step 02-proto3.
#
# Built with steps/02-proto3/tests/ as the Docker build context, so it cannot
# reach the task root or another step; shared helpers are copied per step.
#
# conformance_test_runner must be able to start: COPY --from= moves the binary
# without its dependency closure, so the final stage installs libjsoncpp25 and
# the assertion below checks that the runner loads.
#
# The base carries the upstream conformance suite and gRPC interop binaries
# only. It has no protoc, no libprotobuf and no gRPC library, so this verifier
# cannot grade the candidate against a copy of the thing it reimplements.
#
# Every expected descriptor, encoded message and text-format rendering under
# /tests/corpus3/expect was produced by the official protoc 32.1 release binary
# at authoring time. No oracle runs during scoring.

# >>> self-contained base (identical in every step of this task) >>>
# ---------------------------------------------------------------------------
# Base for protobuf-grpc-edition-chain, built from public sources.
#
# The upstream conformance runner and the gRPC interop binaries are built in
# throwaway stages.  The final stage copies only those executables, so the
# graded image still carries NO protoc, NO libprotobuf and NO gRPC library --
# the property that stops a verifier grading the candidate against a copy of
# the thing it is meant to reimplement, and stops a candidate linking one.
# ---------------------------------------------------------------------------
ARG PROTOBUF_TAG=v32.1
ARG GRPC_GO_VERSION=v1.83.2
ARG GRPCURL_VERSION=v1.9.4

FROM ubuntu:24.04 AS pbbuild
ARG PROTOBUF_TAG
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake git ca-certificates libjsoncpp-dev \
    && rm -rf /var/lib/apt/lists/*
# protobuf's conformance CMake links -ljsoncpp_static; Debian/Ubuntu ship only
# the shared library, so point that name at it. The runner links jsoncpp
# dynamically; the final stage installs libjsoncpp25 for it.
RUN ln -s libjsoncpp.so /usr/lib/x86_64-linux-gnu/libjsoncpp_static.so
RUN git clone --depth 1 -b ${PROTOBUF_TAG} --recurse-submodules --shallow-submodules \
        https://github.com/protocolbuffers/protobuf.git /src/protobuf
RUN cd /src/protobuf \
 && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
        -Dprotobuf_BUILD_TESTS=OFF \
        -Dprotobuf_BUILD_CONFORMANCE=ON \
        -Dprotobuf_ABSL_PROVIDER=module \
        -Dprotobuf_JSONCPP_PROVIDER=package \
        -DCMAKE_CXX_STANDARD=17 \
        -DCMAKE_CXX_FLAGS=-I/usr/include/jsoncpp \
 && cmake --build build --target conformance_test_runner -j"$(nproc)" \
 && install -D "$(find build -name conformance_test_runner -type f -perm -u+x | head -1)" \
        /out/bin/conformance_test_runner \
 && mkdir -p /out/proto \
 && cp conformance/conformance.proto \
       src/google/protobuf/test_messages_proto2.proto \
       src/google/protobuf/test_messages_proto3.proto \
       conformance/test_protos/test_messages_edition2023.proto \
       /out/proto/

FROM golang:1.25-bookworm AS gobuild
ARG GRPC_GO_VERSION
ARG GRPCURL_VERSION
ENV GOBIN=/out/bin CGO_ENABLED=0
RUN go install google.golang.org/grpc/interop/client@${GRPC_GO_VERSION} \
 && go install google.golang.org/grpc/interop/server@${GRPC_GO_VERSION} \
 && go install github.com/fullstorydev/grpcurl/cmd/grpcurl@${GRPCURL_VERSION} \
 && mv /out/bin/client /out/bin/interop_client \
 && mv /out/bin/server /out/bin/interop_server

# ---------------------------------------------------------------------------
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential make gcc g++ libc6-dev zlib1g-dev \
      python3 ca-certificates file binutils xxd procps \
      coreutils diffutils findutils libjsoncpp25 \
 && rm -rf /var/lib/apt/lists/*

COPY --from=pbbuild /out/bin/conformance_test_runner /opt/verifier/bin/
COPY --from=pbbuild /out/proto/ /opt/verifier/proto/
COPY --from=gobuild /out/bin/interop_client  /opt/verifier/bin/
# interop_server and grpcurl are not shipped: no scorer uses them, and a
# candidate must not be able to run a reference implementation.

# A missing or unrunnable instrument must break the build, not score the field:
# run_step06.py would otherwise hand every candidate 0 for grpc_interop, which
# is indistinguishable from a candidate that built no server at all.
# The graders run as root; the candidate uid cannot search this directory.
RUN chmod 0755 /opt/verifier/bin/* && chmod 0700 /opt/verifier/bin \
 && /opt/verifier/bin/conformance_test_runner 2>&1 | head -2 \
 && ! /opt/verifier/bin/conformance_test_runner 2>&1 | grep -q 'error while loading shared libraries' \
 && /opt/verifier/bin/interop_client -help 2>&1 | grep -q server_host \
 && ! command -v protoc \
 && ! ldconfig -p | grep -q 'libprotobuf\|libgrpc'
# <<< self-contained base <<<

RUN mkdir -p /app /logs/verifier

# Assert the tools every anti-vendor gate depends on. A verifier image missing
# these would pass those gates silently, so it must not be buildable at all.
RUN for t in nm strings readelf file md5sum python3 od find; do \
        command -v "$t" >/dev/null || { echo "verifier image lacks $t"; exit 1; }; \
    done

# The conformance runner is this step's main instrument: assert it starts.
RUN /opt/verifier/bin/conformance_test_runner --help 2>&1 | grep -q 'Usage:' \
 && ! /opt/verifier/bin/conformance_test_runner --help 2>&1 | grep -q 'error while loading shared libraries'

COPY . /tests

# ------------------------------------------------ which step built this image
# Docker's build-context sync treats a file as unchanged when (relpath, size,
# mtime) all match, so a SIBLING step's Dockerfile can be used to build this
# step's verifier image. Record the step whose Dockerfile actually produced
# this image; test.sh prints it beside its own id. The pad below gives every
# per-step Dockerfile a byte size no sibling shares; mtimes are separated by whole
# seconds, since GNU tar stores mtime at 1 s granularity.
# collision-pad: ..
RUN mkdir -p /opt/verifier && printf '%s\n' '02-proto3' > /opt/verifier/dockerfile-step-id

# `make` runs as `candidate` BEFORE the runner starts, with /tests readable, so a
# candidate could otherwise harvest the answers at build time and read them back
# while being scored. The runner's own load-then-delete covers the scoring window;
# this covers the build window. Both are needed.
# The refusal inputs and their valid twins (refuse/, reject/) are root-only for
# the same reason; the scorer reads them into memory and deletes them before
# any scored run, and runs each from a fresh directory under a random name.
RUN find /tests -type d \( -name 'expect*' -o -name refuse -o -name reject \) -exec chown root:root {} + \
 && find /tests -type d \( -name 'expect*' -o -name refuse -o -name reject \) -exec chmod 700 {} +
RUN chmod +x /tests/test.sh /tests/*.sh 2>/dev/null || true

WORKDIR /app
