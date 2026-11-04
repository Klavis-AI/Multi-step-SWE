FROM debian:bookworm-slim

# Toolchain for a header-only C/C++ library that is graded by compiling a
# harness against it: a C compiler, cmake for the project's own suite, git for
# the one clone, and nothing else that matters.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash bc ca-certificates cmake coreutils diffutils g++ gcc gawk git make patch procps \
       python3 ripgrep time util-linux \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY timer.sh /app/timer.sh
COPY vendor/ /tmp/vendor/
RUN chmod +x /app/timer.sh

# The starting tree: upstream at the pinned tag with every kernel family's
# optimized backends removed except `find`'s, for every architecture, so the
# whole library runs on the portable reference implementations it ships beside
# them.  It builds, it passes its own suite, and it produces byte-identical
# output -- it is simply slow in the places whose backends are missing.
#
# `strip.py` also collapses the `#if SZ_USE_<TIER>` branches that named those
# backends, in the dispatch headers, the tests and the benchmarks, because the
# tier macros are the compiler's own target macros and stay 1 on a machine that
# can compile AVX2 whatever this tree contains.
#
# The submodule is needed only by the project's own test targets.
RUN . /tmp/vendor/PIN \
    && git clone --quiet --recurse-submodules --shallow-submodules "$SZ_REPO" /app/stringzilla \
    && cd /app/stringzilla \
    && git checkout --quiet "$SZ_SHA" \
    && test "$(git rev-parse HEAD)" = "$SZ_SHA" \
    && test "$(git describe --tags)" = "$SZ_TAG" \
    && rm -rf .git forkunion/.git \
    && python3 /tmp/vendor/strip.py /app/stringzilla \
         cipher hash intersect memory sort utf8_graphemes utf8_linebreaks utf8_norm \
         utf8_runes utf8_sentences utf8_tokens utf8_uncased utf8_uncased_fold utf8_wordbreaks

# The measurement tool the task ships, in source form.  It has to be source:
# the library is header-only, so the harness is compiled against whatever
# headers are in the tree and must be rebuilt after every change.  Its workload
# profile is the practice one -- a different seed, a different size and a
# different script mix from the profile the verifier compiles in -- so a
# measurement can be reproduced locally without exposing the scored draw.  The
# same tool serves every step.
RUN mkdir -p /app/tools \
    && gcc -DPRACTICE_PROFILE -O2 -Wno-unknown-pragmas \
         -mavx2 -mbmi -mbmi2 -maes -mpclmul -msse4.2 -mlzcnt -mpopcnt \
         -o /app/tools/measure /tmp/vendor/measure.c -I /app/stringzilla/include \
    && cp /tmp/vendor/measure.c /app/tools/measure.c \
    && printf '%s\n' \
         '#!/bin/sh' \
         '# Rebuild the timing tool against the tree as it is now.  Compile it' \
         '# with the same capability envelope the grader uses.' \
         'set -e' \
         'exec gcc -DPRACTICE_PROFILE -O2 -Wno-unknown-pragmas \' \
         '    -mavx2 -mbmi -mbmi2 -maes -mpclmul -msse4.2 -mlzcnt -mpopcnt \' \
         '    -o /app/tools/measure /app/tools/measure.c -I /app/stringzilla/include' \
         > /app/tools/build.sh \
    && chmod +x /app/tools/build.sh \
    && rm -rf /tmp/vendor

# The workload is generated inside the harness from a compiled-in profile, so
# there is no corpus file to ship and nothing on disk for a candidate to read.
RUN mkdir -p /app/workload
