# Agent environment for pdf-library-version-chain.
#
# A C development image and nothing that reads PDF: there is no poppler, no qpdf, no
# mupdf, no ghostscript, no pdfium and no PDF library of any kind here, and none in the
# verifier image either, so a candidate that shells out to one cannot be graded by one.
#
# zlib is present because this milestone's filter list names FlateDecode; it is a
# general-purpose compression library and does not parse PDF. No other format-specific
# codec is installed.
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash ca-certificates coreutils curl diffutils file findutils git \
        gawk grep less make patch procps python3 ripgrep sed tar vim-tiny xxd xz-utils \
        gcc gdb binutils libc6-dev zlib1g-dev valgrind \
    && rm -rf /var/lib/apt/lists/*

# Agent harnesses need node and npm on PATH; without them Harbor's setup runs
# `apt-get install nodejs npm` (no --no-install-recommends) into this image.
# curl, bash and pgrep come from the apt block, node and npm from this block.
# Keep this block (or anything else that provides `node` and `npm`) unless the
# task deliberately wants Debian's node packages.
ARG NODE_VERSION=24.22.0
RUN set -eux; \
    arch="$(dpkg --print-architecture)"; \
    case "$arch" in \
      amd64) node_arch=x64 ;; \
      arm64) node_arch=arm64 ;; \
      *) echo "unsupported architecture: $arch" >&2; exit 1 ;; \
    esac; \
    tarball="node-v${NODE_VERSION}-linux-${node_arch}.tar.xz"; \
    curl -fsSLo "/tmp/${tarball}" "https://nodejs.org/dist/v${NODE_VERSION}/${tarball}"; \
    curl -fsSLo /tmp/SHASUMS256.txt "https://nodejs.org/dist/v${NODE_VERSION}/SHASUMS256.txt"; \
    (cd /tmp && grep " ${tarball}\$" SHASUMS256.txt | sha256sum -c -); \
    tar -xJf "/tmp/${tarball}" -C /usr/local --strip-components=1 --no-same-owner; \
    rm -f "/tmp/${tarball}" /tmp/SHASUMS256.txt; \
    test ! -e /usr/share/nodejs; \
    node --version; npm --version

WORKDIR /app

# Advisory countdown; Harbor's steps.agent.timeout_sec is the enforcing limit.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh

# Normative documents and the public (visible) test corpus live outside /app so they
# are not collected with the workspace artifact at the end of every step.
COPY pdfspec /opt/pdfspec
COPY tests-public /opt/pdf-tests-public
RUN chmod -R a+rX /opt/pdfspec /opt/pdf-tests-public

# Documentation for the agent, which works with no network: the specs and
# manuals the instructions cite, stored in environment/docs/ and copied
# directly into the image.
# /opt/docs is outside /app because /app is the workspace collected for
# grading.
COPY docs/ /opt/docs/
