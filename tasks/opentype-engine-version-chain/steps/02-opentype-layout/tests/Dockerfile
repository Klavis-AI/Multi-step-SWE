# Verifier image for step 02.  Built with steps/02-opentype-layout/tests/ as
# the Docker build context, so it cannot reach the task root or another step.
#
# The base carries the reference implementations this milestone is graded
# against — FreeType 2.14.3 with freetype-demos' ftlint, HarfBuzz 14.4.0's
# utilities, OTS 9.3.0, fontTools 4.65.0 — plus the Node.js runtime used to run
# the candidate's engine.  None of that exists in the agent's image.

# >>> self-contained base (generated; identical in every step of this task) >>>
# ---------------------------------------------------------------------------
# Reference toolchain for the opentype verifiers, built from public sources.
#
# FreeType 2.14.3 + freetype-demos' ftlint, HarfBuzz 14.4.0's utilities,
# OTS 9.3.0 and fontTools 4.65.0 are the reference implementations this task is
# graded against.  None of them exists in the agent's image.
# ---------------------------------------------------------------------------
FROM node:22-bookworm-slim

ARG FT_TAG=VER-2-14-3
ARG FTDEMOS_TAG=VER-2-14-3
ARG HB_TAG=14.4.0
ARG OTS_TAG=v9.3.0

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential ca-certificates cmake curl git gperf meson ninja-build \
        pkg-config python3 python3-pip python3-venv ragel xz-utils \
        libglib2.0-dev libcairo2-dev libpng-dev libbrotli-dev zlib1g-dev \
        libbz2-dev file less vim \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 -b ${FT_TAG} https://gitlab.freedesktop.org/freetype/freetype.git /src/freetype \
 && cd /src/freetype \
 && meson setup build --prefix=/usr/local -Dharfbuzz=disabled -Dpng=enabled \
        -Dbrotli=enabled -Dbzip2=enabled -Dzlib=system -Ddefault_library=shared \
 && ninja -C build \
 && ninja -C build install \
 && ldconfig

RUN git clone --depth 1 -b ${FTDEMOS_TAG} https://gitlab.freedesktop.org/freetype/freetype-demos.git /src/ftdemos \
 && cd /src/ftdemos/src \
 && for prog in ftlint ftsbit ftdump; do \
        gcc -O2 -DUNIX -o /usr/local/bin/$prog $prog.c common.c md5.c mlgetopt.c \
            $(pkg-config --cflags freetype2) $(pkg-config --libs freetype2) -lm \
        || gcc -O2 -DUNIX -D'gettext(x)=(x)' -o /usr/local/bin/$prog $prog.c common.c md5.c mlgetopt.c \
            $(pkg-config --cflags freetype2) $(pkg-config --libs freetype2) -lm \
        || echo "SKIPPED $prog" ; \
    done \
 && ldconfig

RUN git clone --depth 1 -b ${HB_TAG} https://github.com/harfbuzz/harfbuzz.git /src/harfbuzz \
 && cd /src/harfbuzz \
 && meson setup build --prefix=/usr/local -Dtests=disabled -Ddocs=disabled \
        -Dutilities=enabled -Dfreetype=enabled -Dglib=enabled -Dcairo=enabled \
        -Draster=enabled -Dvector=enabled -Dgpu=disabled -Dintrospection=disabled \
 && ninja -C build \
 && ninja -C build install \
 && ldconfig

RUN git clone --depth 1 -b ${OTS_TAG} --recurse-submodules https://github.com/khaledhosny/ots.git /src/ots \
 && cd /src/ots \
 && meson setup build --prefix=/usr/local \
 && ninja -C build \
 && ninja -C build install \
 && ldconfig

RUN pip3 install --break-system-packages --no-cache-dir fonttools==4.65.0 brotli lxml

# Fail the build if any reference tool is missing: a verifier that silently
# lacks its instrument would score every candidate alike.
RUN ftlint 2>&1 | head -3 ; hb-shape --version ; hb-vector --version ; hb-subset --version ; \
    ots-sanitize --version 2>&1 | head -2 ; \
    python3 -c "import fontTools; print('fontTools', fontTools.version)" ; \
    node --version

WORKDIR /work
# <<< self-contained base <<<

# /app is repopulated from the collected artifact and /logs/verifier is bind-
# mounted at run time; creating them keeps workdir valid at container start.
RUN mkdir -p /app /logs/verifier

COPY . /tests
RUN chmod +x /tests/test.sh

WORKDIR /app

# ---------------------------------------------------------------------------
# Build-context size pad.  DO NOT DELETE, DO NOT "TIDY".
# harbor tars this tree (GNU tar: 1-second mtime granularity) and a git clone
# gives every file one checkout mtime, so mtime cannot distinguish two steps.
# docker's build-context sync then treats (path, size, mtime) as identity and
# can hand step N's verifier build a SIBLING step's Dockerfile.  Steps 02/03
# and 05/06 differ only in the line-1 comment and would otherwise have
# identical sizes.
# The '#' run below keeps every step's Dockerfile at a unique byte count.
# Regenerate the run length if you edit this file; sizes must stay distinct.
######
