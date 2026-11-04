FROM debian:bookworm-slim

# Build toolchain for mlkem-native: a C99 compiler and make are all its default
# build needs; nasm/yasm are not used by this tree (the x86_64 backend is GAS
# syntax assembled by the C compiler driver).
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash bc ca-certificates coreutils curl diffutils file g++ gcc git less \
       libc6-dev make patch procps python3 ripgrep time util-linux vim-tiny \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY timer.sh /app/timer.sh
COPY vendor/ /tmp/vendor/
RUN chmod +x /app/timer.sh

# Base repository: upstream mlkem-native at the pinned tag, in its own
# default configuration for the arithmetic backend -- the portable C backend.
# The optimized AVX2 arithmetic kernels are not in this tree: a candidate is
# asked to write one stage of them back, and every other copy of them has to go
# with them, or the answer is in the workspace. Removed for that reason:
#
#   mlkem/src/native/{aarch64,ppc64le,riscv64}   same primitives, other ISAs
#   dev/                                         the generator the x86_64
#                                                assembly is derived from
#   proofs/                                      proofs/hol_light/x86_64/mlkem/
#                                                holds a verbatim copy of every
#                                                kernel; the .ml files hold the
#                                                constant tables
#   scripts/{autogen,simpasm}                    regenerates the constant
#                                                tables and re-emits dev/
#   examples/, integration/                      demos; one demo is a build of
#                                                the amalgamation
#   mlkem/mlkem_native_asm.S, .c                 the amalgamations embed the
#                                                removed files by path
#
# The amalgamated header mlkem_native.h stays: the project's own suites include
# it. test/abicheck/ stays too -- it is upstream's own ABI-conformance harness,
# the build system includes its makefile unconditionally, and removing it breaks
# the build. It contains signatures and register conventions, not kernels. The
# project's own component benchmark stays as well; it names the same dispatch
# hooks by construction.
#
# Everything else is upstream and intact -- the build system, the functional
# and known-answer suites (which this tree passes in full), the C backend, and
# the FIPS-202 (Keccak) native backend, which is a different algorithm family
# and is not part of this ladder.
RUN . /tmp/vendor/PIN \
    && git clone --quiet --depth 1 --branch "$MLK_TAG" "$MLK_REPO" /app/mlkem-native \
    && cd /app/mlkem-native \
    && test "$(git rev-parse HEAD)" = "$MLK_SHA" \
    && rm -rf .git \
    && rm -rf mlkem/src/native/aarch64 mlkem/src/native/ppc64le \
              mlkem/src/native/riscv64 dev proofs examples integration \
              scripts/autogen scripts/simpasm \
              mlkem/mlkem_native.c mlkem/mlkem_native_asm.S \
    && python3 /tmp/vendor/strip.py /app/mlkem-native 0

# Prove the shipped base builds and passes the project's own suites before it
# is handed to anyone, then drop the tooling: strip.py enumerates every stage
# of the ladder and the harness names every scored entry point, so neither may
# survive into the agent's image.
RUN cd /app/mlkem-native \
    && make lib OPT=1 AUTO=1 -j4 BUILD_DIR=/tmp/seedbld > /dev/null \
    && make run_func run_kat OPT=1 AUTO=1 -j4 BUILD_DIR=/tmp/seedbld > /dev/null \
    && echo "base tree builds and passes func + kat" \
    && rm -rf /tmp/seedbld /tmp/vendor
