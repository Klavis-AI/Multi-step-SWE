# Agent image.  A plain Java 25 development box.
#
# The tools whose output this task is graded against are deliberately absent:
# no LLVM binaries, and none of binutils' or elfutils' debug readers.  This is
# a plain JDK + gcc box.
FROM debian:trixie-slim

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash ca-certificates coreutils curl diffutils file findutils git \
        gcc g++ less make patch procps python3 ripgrep sed tar vim-tiny xz-utils \
        openjdk-25-jdk-headless zlib1g \
    && rm -rf /var/lib/apt/lists/* \
    # remove the DWARF-reading binaries that ship with binutils; keep the ones
    # gcc needs to assemble and link (as, ld, ar, ranlib, strip, objcopy)
    && for t in readelf objdump addr2line nm strings size c++filt gprof dwp \
                x86_64-linux-gnu-readelf x86_64-linux-gnu-objdump \
                x86_64-linux-gnu-addr2line x86_64-linux-gnu-nm \
                x86_64-linux-gnu-strings x86_64-linux-gnu-size \
                x86_64-linux-gnu-c++filt eu-readelf eu-stack eu-addr2line \
                eu-nm eu-objdump gdb gdbserver gcore; do \
         rm -f "/usr/bin/$t" "/usr/local/bin/$t"; \
       done \
    && rm -f /usr/bin/llvm-* /usr/bin/*-llvm-* 2>/dev/null || true

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

COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh

# A JDK writes nothing outside the workspace during a normal build; keep the
# default locale and time zone fixed so a candidate's own output is stable.
ENV LC_ALL=C TZ=UTC JAVA_TOOL_OPTIONS=-Dfile.encoding=UTF-8

# Documentation for the agent, which works with no network: the specs and
# manuals the instructions cite, stored in environment/docs/. They were
# downloaded once from their official sources and checked against pinned
# sha256 sums; docs-provenance.txt records the URLs, sums and conversions.
# /opt/docs is outside /app because /app is the workspace collected for
# grading.
COPY docs/ /opt/docs/
