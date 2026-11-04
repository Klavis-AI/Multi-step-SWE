# Verifier image for one step, built with that step's tests/ as the Docker
# build context.  It carries no build of its own: the candidate tree is assembled
# and built by test.sh from the candidate's sources, and the scalar reference
# the measurement divides by lives in that same tree, so there is nothing to
# pre-build here.
#
# This file is identical for every step.  What differs is the data files
# FUNCS.step02 / FILTERS.step02 / GEOMETRY.step02 --
# every one of them named with its step number, so two steps' copies can never
# be confused for one another by the build context cache.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash bc ca-certificates cmake coreutils diffutils g++ gcc gawk git make \
       ninja-build patch procps python3 tar time util-linux \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /logs/verifier /tests/vendor

# Step identity, for the binding check at the top of test.sh.
#
# Written BEFORE the COPY below, so it names this IMAGE even when the copy is
# served from another step's cache entry.  The builder identifies a build
# context file by (relative path, size, mtime) and never reads its bytes, so
# two steps whose tests/ trees share a filename, a size and a second are
# indistinguishable to it -- and the wrong milestone's tests get baked in.
# Nothing inside a Dockerfile prevents that; this only reports it.
#
# Written as a LITERAL rather than as an ARG: the verifier build declares no
# `args:`, so no --build-arg can reach this build and an ARG default would be an
# override surface nothing can use.
#
# Keep this number, the directory's number and test.sh's check equal.
RUN echo "02" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh /tests/vendor/run_test.sh

# The pinned release, kept whole: test.sh restores the frozen set (the project's
# own test framework, the scalar reference implementations and the build system)
# from here, so the instrument that measures a candidate is never the copy the
# candidate could have edited, and the denominator can never be slowed down.
RUN . /tests/vendor/PIN \
    && git clone --quiet --depth 1 --branch "$SIMD_TAG" "$SIMD_REPO" /pristine \
    && cd /pristine \
    && test "$(git rev-parse HEAD)" = "$SIMD_SHA" \
    && rm -rf /pristine/.git

WORKDIR /app
