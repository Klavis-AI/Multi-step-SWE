FROM debian:bookworm-slim

# Build toolchain for the Arrow C++ tree, plus the system Thrift that Arrow's
# Parquet component needs (building Arrow's bundled Thrift requires CMake >= 3.26
# and bookworm ships 3.25).
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash bc ca-certificates ccache cmake coreutils curl diffutils g++ gcc git \
       libthrift-dev make patch procps python3 python-is-python3 ripgrep time \
       util-linux \
    && rm -rf /var/lib/apt/lists/*

ENV CCACHE_DIR=/ccache
ENV CCACHE_MAXSIZE=20G

WORKDIR /app

COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh

COPY vendor/ /tmp/vendor/

# The source tree for this milestone: upstream Arrow at the pinned commit with
# the optimized implementations of every kernel in this ladder removed, for
# every architecture.  What is left is complete and correct -- it builds, it
# passes its own test suite, and every kernel falls back to the portable scalar
# code in the same tree.  It is simply slow where the optimized code is gone.
#
# strip.py lists every stage of the ladder, so it is deleted with the rest of
# /tmp/vendor at the end of this file.  The pin does not ship either: it names
# the exact upstream commit whose code the candidate is asked not to copy.
RUN . /tmp/vendor/PIN \
    && git clone --quiet "$ARROW_REPO" /app/arrow \
    && cd /app/arrow \
    && git checkout --quiet "$ARROW_SHA" \
    && test "$(git rev-parse HEAD)" = "$ARROW_SHA" \
    && rm -rf .git \
    && python3 /tmp/vendor/strip.py /app/arrow $(cat /tmp/vendor/base_stages.txt)

# Build it once, with the same configuration the checks use, so the candidate
# spends its two hours on the kernel rather than on a first compile.  The build
# directory is excluded from the step artifact and is never sent for checking.
#
# Only the seven test targets the checks run are built, not all of Arrow's --
# building the rest doubles the image build and none of it covers a component
# this ladder touches.
RUN cd /app \
    && BUILD_JOBS="$(nproc)" \
       BUILD_TARGETS="arrow_static arrow_compute_static parquet_static arrow_acero_static \
arrow-utility-test arrow-bit-utility-test parquet-encoding-test parquet-reader-test \
parquet-internals-test arrow-compute-row-test arrow-acero-hash-join-node-test" \
       bash /tmp/vendor/build_arrow.sh /app/arrow /app/build \
    && rm -rf /app/build/CMakeFiles/*/CompilerId* || true

# strip.py lists every stage of the ladder and PIN names the exact upstream
# commit whose code the candidate is asked not to copy; neither belongs in the
# agent's container.  The per-milestone measurement tools arrive with each
# milestone's workdir instead, so each one knows only its own functions.
RUN rm -rf /tmp/vendor

# zlib's development package, needed to LINK anything against Arrow: libarrow.a
# references deflate/inflate from its compression translation units, so the
# practice harness the milestone asks the candidate to build dies at the link
# step without it.  Installed here, at the end, rather than in the toolchain
# list at the top: adding it there would invalidate the ~55-minute Arrow build
# above, and this is the only thing that needs it.
RUN apt-get update \
    && apt-get install -y --no-install-recommends zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*
