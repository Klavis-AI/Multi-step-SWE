# Agent environment for the isa-l_crypto multi-buffer hashing ladder.
#
# Two stages on purpose. The first clones the pinned upstream tree and removes
# the ladder stages' optimized implementations; only the *stripped* result is
# copied into the final image. A single-stage build would keep the pristine
# tree -- every kernel the candidate is asked to write -- in an intermediate
# layer of the image it runs in, recoverable from the layer store or from
# `docker history`. The build stage's layers are not part of the final image.
#
# What is removed, per stage directory: every *.asm (the multi-buffer kernels,
# the lane-scheduling manager and the *_multibinary.asm dispatcher) and the
# aarch64/ and riscv64/ siblings that implement the same widening for another
# ISA as .S and as readable C intrinsics. What stays: the portable C reference
# (*_ctx_base.c, *_ref.c, *_base_aliases.c) and upstream's own *_base_aliases
# Makefile/CMake wiring, which is the tree's documented scalar-C configuration.

FROM debian:bookworm-slim AS stripper

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates coreutils libc6-dev git make nasm patch python3 \
    && rm -rf /var/lib/apt/lists/*

COPY vendor/ /tmp/vendor/

RUN . /tmp/vendor/PIN \
    && git clone --quiet "$ISAL_REPO" /src/isa-l_crypto \
    && cd /src/isa-l_crypto \
    && git checkout --quiet "$ISAL_SHA" \
    && test "$(git rev-parse HEAD)" = "$ISAL_SHA" \
    && rm -rf .git \
    && python3 /tmp/vendor/strip.py /src/isa-l_crypto \
         sha256_mb sha1_mb sha512_mb md5_mb sm3_mb \
         mh_sha1 mh_sha1_murmur3_x64_128 mh_sha256 rolling_hash \
    && make -f Makefile.unx Makefile.nmake > /dev/null 2>&1


FROM debian:bookworm-slim

# The toolchain the tree's own Makefile.unx needs, plus the handful of shell
# tools the task's instructions use. libssl-dev is here because the *_mb
# perf and rand_ssl tests link OpenSSL.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash bc ca-certificates coreutils diffutils gawk gcc git libc6-dev libssl-dev \
       make nasm patch procps python3 ripgrep time util-linux \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh

COPY --from=stripper /src/isa-l_crypto /app/isal_crypto

# The practice measurement tool. Its source is deliberately visible: it names
# the operations and the arithmetic, and nothing about the graded workload,
# whose inputs are drawn inside the verifier image from a verifier-owned seed.
COPY vendor/practice.c /app/tools/practice.c
COPY vendor/measure.sh /app/tools/measure.sh
RUN chmod +x /app/tools/measure.sh

# Prove the delivered tree builds before it is handed over, then leave it
# clean so the candidate's first build is a real build.
RUN cd /app/isal_crypto \
    && make -f Makefile.unx lib -j4 > /tmp/preseed.log 2>&1 \
    && rm -rf bin \
    && find . -maxdepth 1 -name '*.o' -delete \
    || { echo "PRESEED BUILD FAILED"; tail -40 /tmp/preseed.log; exit 1; }
