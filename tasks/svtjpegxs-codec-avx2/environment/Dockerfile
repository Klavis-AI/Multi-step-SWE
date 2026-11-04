FROM debian:bookworm-slim

# Toolchain for a CMake/C project with a hand-written x86 assembly tier.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash bc ca-certificates cmake coreutils diffutils g++ gcc gawk git make \
       nasm ninja-build patch procps python3 time util-linux \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY timer.sh /app/timer.sh
COPY vendor/ /tmp/vendor/
RUN chmod +x /app/timer.sh

# The working tree: SVT-JPEG-XS at the pinned commit, with the runtime-selected
# x86 implementations of the pipeline stages this ladder is about removed, and
# the build system pruned of the AVX-512 tier.  What is left is a complete and
# conforming JPEG XS encoder/decoder -- it builds, it passes its own suite, and
# it encodes and decodes byte-identically -- running its portable C paths.
#
# The portable C for every stage stays in the tree: it is the correctness
# reference and it is the denominator every step's timing is taken against.
RUN . /tmp/vendor/PIN \
    && git clone --quiet "$SVTPIN_REPO" /app/svt-jpeg-xs \
    && cd /app/svt-jpeg-xs \
    && git checkout --quiet "$SVTPIN_SHA" \
    && test "$(git rev-parse HEAD)" = "$SVTPIN_SHA" \
    && rm -rf .git \
    && python3 /tmp/vendor/prepare.py /app/svt-jpeg-xs /tmp/prep \
    && rm -rf /app/svt-jpeg-xs \
    && mv /tmp/prep /app/svt-jpeg-xs \
    && python3 /tmp/vendor/strip.py /app/svt-jpeg-xs \
         nlt_enc dwt_fwd quant gcli rate_ctrl unpack idwt nlt_dec

# The agent's measurement tool: the same per-function harness used for
# measurement, as source, plus a binary of it linked against THIS tree so there
# is a denominator to divide by. Its build directory is thrown away with the
# rest of /tmp; the source stays under /app/tools/ so a candidate can rebuild
# it against its own work.
RUN mkdir -p /app/tools/bench \
    && cp /tmp/vendor/bench/*.c /tmp/vendor/bench/*.h /app/tools/bench/ \
    && cp /tmp/vendor/build_bench.sh /app/tools/build_bench.sh \
    && chmod +x /app/tools/build_bench.sh \
    && bash /app/tools/build_bench.sh /app/svt-jpeg-xs /tmp/seedbld /app/tools/bench /app/tools/codecbench-base \
    && cp -a /tmp/seedbld/out /app/tools/l0 \
    && rm -rf /tmp/seedbld /tmp/vendor

# A 1080p and a 320x240 sample picture, for the candidate's own runs.  These are
# samples: the measurement regenerates its own input and never reads /app.
RUN mkdir -p /app/samples \
    && cp /app/svt-jpeg-xs/Build/linux/PGO/1080p_422_8b.yuv /app/samples/1080p_422_8b.yuv

WORKDIR /app
