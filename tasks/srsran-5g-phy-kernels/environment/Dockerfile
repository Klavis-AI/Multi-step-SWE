FROM debian:bookworm-slim

# Build toolchain plus the dependencies srsRAN's own install script names.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash bc ca-certificates ccache cmake coreutils curl diffutils g++ gcc git \
       libfftw3-dev libgtest-dev libmbedtls-dev libsctp-dev libyaml-cpp-dev \
       make patch pkg-config procps python3 ripgrep time util-linux \
    && rm -rf /var/lib/apt/lists/*

# Keep the compile cache inside the image, so the tree ships prebuilt and the
# agent's first rebuild is incremental rather than twenty minutes of cold
# compilation.
ENV CCACHE_DIR=/root/.ccache
RUN ccache --max-size=4G > /dev/null

WORKDIR /app

COPY timer.sh /app/timer.sh
COPY vendor/ /tmp/vendor/
RUN chmod +x /app/timer.sh

# Base repository for step 1: upstream at the pinned commit with every stage of
# this ladder removed. The tree is complete and correct -- it builds, it passes
# its own suite, and it encodes and decodes everything through the portable
# implementations. It is simply slow in the places whose optimized code is
# missing.
#
# The optimizations are removed for EVERY architecture, not just x86: upstream
# ships the same kernels again as NEON C for aarch64, and leaving those would be
# a translation exercise with the answer in the workspace.
RUN . /tmp/vendor/PIN \
    && git clone --quiet "$SRS_REPO" /app/srsran \
    && cd /app/srsran \
    && git checkout --quiet "$SRS_SHA" \
    && test "$(git rev-parse HEAD)" = "$SRS_SHA" \
    && rm -rf .git \
    && python3 /tmp/vendor/strip.py /app/srsran \
         ldpc_encode ldpc_rate_dematch ldpc_decode precode demodulate ofh_bfp ofh_pack

# Prebuild, so the shipped tree is one incremental build away from runnable.
RUN . /tmp/vendor/PIN \
    && cmake -S /app/srsran -B /app/build $SRS_CMAKE_OPTS > /dev/null \
    && cmake --build /app/build -j4 --target $SRS_LIB_TARGETS $SRS_TEST_TARGETS > /dev/null

# The measurement tool. It cannot be shipped as a compiled binary: it links the
# tree's own static libraries, so it has to be rebuilt against whatever the tree
# looks like now. What is shipped is therefore its source plus a one-line build
# script, and nothing else from the build directory. strip.py and the upstream
# pin are not shipped -- strip.py lists every stage of the ladder, and the pin
# names the commit whose code the task is about.
RUN mkdir -p /app/tools \
    && cp /tmp/vendor/bench_phy.cpp /app/tools/bench_phy.cpp \
    && cp /tmp/vendor/build_harness.sh /app/tools/build_harness.sh \
    && chmod +x /app/tools/build_harness.sh \
    && rm -rf /tmp/vendor

WORKDIR /app
