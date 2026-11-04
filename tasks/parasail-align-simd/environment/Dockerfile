# Two stages on purpose: the seed stage holds the stripper, the upstream pin
# and the harness source, and none of the three is ever copied into the image
# the agent gets.  A single-stage build would leave them in an earlier layer.

FROM debian:bookworm-slim AS seed
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash binutils ca-certificates coreutils gcc git libc6-dev patch python3 \
    && rm -rf /var/lib/apt/lists/*
COPY vendor/ /tmp/vendor/
# The workspace for step 1: upstream at the pinned commit with every alignment
# family's *vectorized* implementations removed.  What stays is the project's
# own non-vectorized reference for each family (src/sw.c, src/nw.c, src/sg.c,
# src/*_stats.c, src/*_trace.c and their _scan twins), and the dispatchers are
# rewired to fall through to them -- so the library is complete and correct, it
# builds, and it passes its own suite.  It is simply slow in the places whose
# optimized code is missing.
#
# contrib/ssw goes with them: it is a complete third-party SIMD Smith-Waterman
# vendored inside parasail's own tree -- SSE2 intrinsics, the striped layout,
# traceback and CIGAR -- and leaving it would make step 1 a copy job rather
# than an optimization.
RUN . /tmp/vendor/PIN \
    && mkdir -p /out \
    && git clone --quiet "$PARASAIL_REPO" /out/parasail \
    && cd /out/parasail \
    && git checkout --quiet "$PARASAIL_SHA" \
    && test "$(git rev-parse HEAD)" = "$PARASAIL_SHA" \
    && rm -rf .git \
    && python3 /tmp/vendor/strip.py /out/parasail \
         sw nw sg sw_stats sw_trace nw_stats nw_trace sg_stats sg_trace
# The agent's only measurement tool: a compiled binary that loads whatever
# libparasail.so it is pointed at and calls the library's own public entry
# points through the method pointers the library installed.  It draws its
# sequences from a seed given on the command line, so nothing in the workspace
# names the graded draw.
RUN gcc -O2 -o /out/pbench /tmp/vendor/bench_stage.c \
        -I /out/parasail -I /out/parasail/src -ldl

FROM debian:bookworm-slim
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash bc binutils ca-certificates coreutils diffutils gcc g++ git make \
       libc6-dev meson ninja-build patch pkg-config procps python3 ripgrep time util-linux zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY timer.sh /app/timer.sh
COPY --from=seed /out/parasail /app/parasail
COPY --from=seed /out/pbench /app/tools/pbench
RUN chmod +x /app/timer.sh /app/tools/pbench

# Pre-build once so the agent starts from a working tree rather than from a
# build failure it has to diagnose first.
RUN meson setup /app/build /app/parasail --buildtype=release > /dev/null \
    && ninja -C /app/build -j"$(nproc)" > /dev/null \
    && rm -rf /app/build
