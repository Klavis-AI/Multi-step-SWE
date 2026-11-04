FROM debian:bookworm-slim

# Build toolchain for mldsa-native: the library is C99 plus x86-64 assembly, and
# the project's own test and benchmark binaries are built with make.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates coreutils diffutils g++ gcc gawk git make patch \
       procps python3 time util-linux \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY timer.sh /app/timer.sh
COPY vendor/ /tmp/vendor/
RUN chmod +x /app/timer.sh

# The tree every milestone starts from: upstream at the pinned commit, with this
# ladder's optimized arithmetic backends removed. What is left is a complete and
# correct ML-DSA implementation -- it builds, it passes the project's own
# functional, differential-unit and KAT suites, and it signs and verifies every
# parameter set correctly -- running the portable C implementation of every
# arithmetic component.
#
# The x86-64 backend directory, its generated constant table, the AArch64
# backend (the same algorithms in NEON), the generator the assembly files are
# derived from, and the machine-checked proofs that embed the compiled bytecode
# of every kernel all go. Keeping any of them turns the task into
# transliteration.
RUN . /tmp/vendor/PIN \
    && git clone --quiet --depth 1 --branch "$MLD_TAG" "$MLD_REPO" /app/mldsa-native \
    && cd /app/mldsa-native \
    && test "$(git rev-parse HEAD)" = "$MLD_SHA" \
    && rm -rf .git \
    && python3 /tmp/vendor/strip.py /app/mldsa-native ntt intt pointwise decompose32 decompose88 rej unpack

# Prove the starting tree builds before the image is published.
RUN cd /app/mldsa-native \
    && make lib OPT=1 AUTO=1 BUILD_DIR=/tmp/seedbld -j"$(nproc)" > /dev/null \
    && rm -rf /tmp/seedbld

RUN rm -rf /tmp/vendor
