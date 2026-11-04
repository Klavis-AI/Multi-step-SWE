FROM debian:bookworm-slim

# Build toolchain for a header-only C++17 library plus the project's own CMake
# build: g++ (one compiler for both sides), cmake/make (the project suite),
# python3 and git for the pinned checkout, plus the usual shell utilities.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash bc ca-certificates cmake coreutils curl diffutils g++ gcc gawk git \
       make patch procps python3 ripgrep time util-linux \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh

# The starting checkpoint: BitMagic at the pin, with this ladder's ten kernel
# families removed from every backend header.  The tree is complete and correct
# -- it builds, it passes its own stress suite, and every entry point in it works
# through the portable C++ bodies in src/bmfunc.h.  It is simply slow in the
# places whose hand-written AVX2 kernels are missing.
#
# strip.py lists every family of the ladder, so it is used here and deleted
# below rather than shipped.
COPY vendor/ /tmp/vendor/
RUN . /tmp/vendor/PIN \
    && git clone --quiet "$BM_REPO" /app/bitmagic \
    && cd /app/bitmagic \
    && git checkout --quiet "$BM_SHA" \
    && test "$(git rev-parse HEAD)" = "$BM_SHA" \
    && rm -rf .git \
    && python3 /tmp/vendor/strip.py /app/bitmagic $(cat /tmp/vendor/ALL_GROUPS) \
    && rm -rf /tmp/vendor

# Provenance of the starting tree, without naming the upstream commit.
# No upstream version string and no commit hash: the candidate image withholds
# the pin deliberately, and a release number is one `git clone -b <tag>` away
# from the answer even though the task rules forbid taking it.
RUN printf '%s\n' \
    "Prepared for this milestone: the kernel families the milestone ladder" \
    "measures have been removed from src/bmavx2.h and the other backend headers," \
    "so the portable C++ bodies in src/bmfunc.h are what runs." \
    > /app/STARTING_TREE.txt

WORKDIR /app
