FROM debian:bookworm-slim

# Toolchain for OpenJPH: the project is CMake + C++14 and needs nothing else.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash bc ca-certificates cmake curl diffutils g++ gcc git make patch \
       procps python3 ripgrep time util-linux \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY timer.sh /app/timer.sh
COPY vendor/ /tmp/vendor/
RUN chmod +x /app/timer.sh

# The workspace tree: upstream OpenJPH at the pinned commit with the optimized
# implementation of the codec's hot stages removed.  The library is complete
# and correct -- it builds, it passes upstream's own 109-test suite, and it
# encodes and decodes every image correctly through the portable C++ paths in
# src/core.  It is simply slow where the optimized code is missing.
#
# The portable implementations are what the library falls back to and they stay
# in the tree: they define the correct output and they are the reference the
# measurement divides by.
RUN . /tmp/vendor/PIN \
    && git clone --quiet --depth 1 --branch "$OJPH_TAG" "$OJPH_REPO" /app/ojph \
    && cd /app/ojph \
    && git checkout --quiet "$OJPH_SHA" \
    && test "$(git rev-parse HEAD)" = "$OJPH_SHA" \
    && rm -rf .git \
    && python3 /tmp/vendor/strip.py /app/ojph \
         blk_decode blk_encode rev_wavelet irv_wavelet colour codestream

# Sample workload.  Publishable and not the graded draw: the graded measurement
# runs per function on inputs the verifier generates from its own seed.
RUN mkdir -p /app/workload \
    && python3 /tmp/vendor/make_workload.py /app/workload

# The candidate's only measurement tool is a compiled binary.  It drives the
# tree the candidate built and prints one number; the input, the transform, the
# quantization and the repetition count are compiled-in integers, so nothing
# about the measurement is recoverable from a readable script or from the
# argument list.  This is a feedback instrument -- the graded measurement runs
# inside the verifier, per function, on inputs the candidate never sees.
#
# strip.py enumerates every stage of the milestone ladder and PIN names the
# commit the candidate is asked not to copy from; neither may reach /app.  Both
# live only in /tmp/vendor, which this layer deletes.
RUN mkdir -p /app/tools \
    && gcc -O2 -o /app/tools/measure /tmp/vendor/measure.c \
    && chmod +x /app/tools/measure \
    && rm -rf /tmp/vendor
