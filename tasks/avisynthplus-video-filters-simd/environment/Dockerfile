FROM debian:bookworm-slim

# AviSynth+ is a C++14 CMake project.  clang is deliberately absent: the task's
# capability envelope pins one compiler for both sides, and the work under test
# is hand-written intrinsics, which clang and gcc compile identically enough
# that offering a choice would only add a way to lose conditionally.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates cmake coreutils diffutils file g++ gcc git make \
       ninja-build patch procps python3 ripgrep time util-linux \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY timer.sh /app/timer.sh
COPY vendor/ /tmp/vendor/
RUN chmod +x /app/timer.sh

# The starting tree: AviSynth+ at the pinned commit with every `*/intel/*` and
# `*/aarch64/*` source removed and every INTEL_INTRINSICS and NEON_INTRINSICS
# conditional evaluated away.  What is left is upstream's own portable C++ for
# every filter -- a complete, correct, supported AviSynth+ that builds with
# upstream's own build command and no special flag, and that simply has no
# vectorized kernels anywhere in the filter set.
#
# The source arrives as a checksummed tarball rather than a clone, and it is the
# same tarball, the same stripper and the same digest every step uses.  A base
# tree built from a different stripper revision than the one the oracle patch
# was generated against rejects every hunk.
RUN . /tmp/vendor/PIN \
    && echo "$AVS_TARBALL_SHA256  /tmp/vendor/pin.tar.gz" | sha256sum -c - \
    && mkdir -p /tmp/pin \
    && tar -xzf /tmp/vendor/pin.tar.gz -C /tmp/pin \
    && python3 /tmp/vendor/mklevel.py /tmp/pin /app/avisynthplus \
    && rm -rf /tmp/pin

# Prove the tree builds before the agent ever sees it, and leave the build tree
# in place so the first milestone starts from a warm configuration.
RUN cmake -S /app/avisynthplus -B /app/build -DCMAKE_BUILD_TYPE=Release -G Ninja \
        > /dev/null \
    && cmake --build /app/build -j"$(nproc)" > /dev/null

# The agent's own measurement tool.  It is a generic driver: give it a script
# and it reports that script's frame time, so it carries no information about
# which scripts are measured.  Its source is deleted in this layer.
#
# The tool is not a security boundary -- a root shell can read any binary -- but
# the measurement scripts live only in the test image, which the agent never
# sees.
#
# /tmp/vendor also held the stripper, which names every family of the ladder,
# and the upstream pin.  Neither belongs in the container the agent works in.
RUN mkdir -p /app/tools \
    && gcc -O2 -o /app/tools/measure /tmp/vendor/avsbench.c \
         -I /app/avisynthplus/avs_core/include -ldl \
    && chmod +x /app/tools/measure \
    && rm -rf /tmp/vendor
