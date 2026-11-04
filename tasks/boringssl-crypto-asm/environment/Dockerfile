FROM debian:bookworm-slim

# BoringSSL needs a C/C++ toolchain, CMake and Ninja. Its perlasm assembly is
# checked in pre-generated under gen/, so no Perl is needed to build the tree as
# it ships; a candidate who wants to author perlasm can install it themselves.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash binutils ca-certificates cmake coreutils curl diffutils g++ gcc git \
       make ninja-build patch perl procps python3 ripgrep time util-linux \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY timer.sh /app/timer.sh
COPY vendor/ /tmp/vendor/
RUN chmod +x /app/timer.sh

# The starting checkpoint: upstream at the pinned commit with the optimized
# implementation of every primitive group in this ladder removed -- the perlasm
# for all four architectures, the pre-generated assembly under gen/, the
# generated build lists, and the dispatch that installs them. The portable
# implementations stay, so the library is complete and correct and simply runs
# the C paths in the places whose kernels are missing.
RUN . /tmp/vendor/PIN \
    && git clone --quiet "$BORINGSSL_REPO" /app/boringssl \
    && cd /app/boringssl \
    && git checkout --quiet "$BORINGSSL_SHA" \
    && test "$(git rev-parse HEAD)" = "$BORINGSSL_SHA" \
    && rm -rf .git \
    && python3 /tmp/vendor/strip.py /app/boringssl \
         aes_block aes_gcm gcm_siv bn_mont p256

# Pre-build once at -j4, so a candidate's first build is incremental rather than
# fifteen minutes of the milestone's budget. Nothing here is privileged: the
# same `cmake --build` works from an empty directory.
RUN cmake -S /app/boringssl -B /app/build -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null \
    && cmake --build /app/build -j4 --target crypto > /dev/null

# strip.py names every stage of the ladder, so shipping it would hand the
# candidate the whole map of what remains. The grading harness and the verifier's
# fixtures live in the verifier image, which the candidate cannot edit. The
# per-milestone measurement tool is written into /app/tools by each milestone's
# workspace handoff, and covers only that milestone's own entry points.
RUN mkdir -p /app/tools && rm -rf /tmp/vendor
