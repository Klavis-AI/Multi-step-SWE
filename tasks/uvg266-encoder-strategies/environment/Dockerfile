FROM debian:bookworm-slim

# Toolchain: cmake + make to build uvg266, gcc for its C and for the strategy
# harness the agent compiles against its own tree.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash bc ca-certificates cmake coreutils curl diffutils g++ gcc gawk git \
       make patch procps python3 ripgrep time util-linux \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY timer.sh /app/timer.sh
COPY vendor/ /tmp/vendor/
RUN chmod +x /app/timer.sh

# Base tree: upstream at the pinned commit with every optimized strategy of
# this ladder removed -- see /tmp/vendor/strip.py for what a stage is. The
# portable C implementations in src/strategies/generic/ stay: they are the
# correctness reference and the thing the measurement divides by. Everything
# else is upstream and intact, including the greatest-based test suite.
RUN . /tmp/vendor/PIN \
    && git clone --quiet "$UVG_REPO" /app/uvg266 \
    && cd /app/uvg266 \
    && git checkout --quiet "$UVG_SHA" \
    && test "$(git rev-parse HEAD)" = "$UVG_SHA" \
    && git submodule update --init --quiet greatest \
    && rm -rf .git .gitmodules \
    && python3 /tmp/vendor/strip.py /app/uvg266 \
         transforms blockcost intra motion quant loopfilter depquant

# A practice harness the agent can rebuild against its own tree. It calls the
# same installed strategy pointers the measurement calls, but it lives here as
# source: it links the candidate's own library, so it has to be recompiled
# after every change, and a prebuilt binary would be useless.
RUN mkdir -p /app/tools \
    && cp /tmp/vendor/bench-practice.c /app/tools/bench.c \
    && cp /tmp/vendor/make_clip.py /app/tools/make_clip.py \
    && cp /tmp/vendor/PIN /app/tools/PIN 2>/dev/null || true

# strip.py enumerates every stage of the ladder, so shipping it would hand the
# candidate the shape of the remaining steps. It lives only in the verifier
# image. Same for the upstream checkout: the package names it, and the network
# can always produce it, but nothing in /app should point at it.
RUN rm -rf /tmp/vendor /app/tools/PIN
