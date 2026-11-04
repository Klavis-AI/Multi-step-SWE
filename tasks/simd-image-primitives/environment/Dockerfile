FROM debian:bookworm-slim

# Build toolchain for the Simd library plus the shell tools the measurement
# and the agent's own checks need.  ccache is here because an optimization task is
# a rebuild-heavy task: it does not change code generation, it only makes the
# agent's second and later builds cheap.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash bc ca-certificates ccache cmake curl diffutils g++ gcc git make \
       ninja-build patch procps python3 ripgrep time util-linux \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY timer.sh /app/timer.sh
COPY vendor/ /tmp/vendor/
RUN chmod +x /app/timer.sh

# Base repository for step 1: upstream at the pinned release with the image
# primitive families of this ladder removed -- their per-ISA implementation
# files for every architecture (x86, NEON, SVE2, HVX, ...) and their dispatch
# branches.  What stays is the complete library on its portable path: the
# scalar reference implementations under src/Simd/SimdBase*.cpp, the whole
# build system, and the project's own test framework.  Everything outside the
# eight families named in the milestone is untouched and still optimized.
#
# The stripper is deleted with the vendor directory in the same layer: it
# enumerates every family of the ladder, so shipping it would hand the agent
# the plan for every later step.
RUN . /tmp/vendor/PIN \
    && git clone --quiet --depth 1 --branch "$SIMD_TAG" "$SIMD_REPO" /app/Simd \
    && cd /app/Simd \
    && test "$(git rev-parse HEAD)" = "$SIMD_SHA" \
    && rm -rf .git \
    && python3 /tmp/vendor/strip.py /app/Simd \
         resizer blur edges cascade hog warp stats background \
    && rm -rf /tmp/vendor
