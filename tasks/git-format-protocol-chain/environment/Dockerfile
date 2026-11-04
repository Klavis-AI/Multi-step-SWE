# Agent environment. PHP 8.2 is the implementation language; the image carries a
# real git for the candidate's own comparisons and git's own public test suite
# at /opt/git-suite, both OUTSIDE the collected /app artifact.

# >>> self-contained base (identical in every step of this task) >>>
# ---------------------------------------------------------------------------
# Base for git-format-protocol-chain, built from public sources.
#
# Stage 1 builds the three pinned real gits and git's own test suite.  The
# final stage carries NO compiler and NO git on PATH: the candidate is rebuilt
# from its own PHP sources and graded only through its own binary and the
# pinned peers under /opt/gitpeer.
# ---------------------------------------------------------------------------
FROM debian:bookworm-slim AS gitbuild

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential ca-certificates curl gettext perl python3 tar xz-utils \
        libcurl4-openssl-dev libexpat1-dev libssl-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

# Pinned by sha256: a peer git that silently changed version would move every
# differential expectation in the corpus.
ARG GIT_2_20_1_SHA256=9d2e91e2faa2ea61ba0a70201d023b36f54d846314591a002c610ea2ab81c3e9
ARG GIT_2_39_5_SHA256=c58da92c378df4a986ca33266897a7397e86c22ee266a284d8c2432c39066b59
ARG GIT_2_51_0_SHA256=60a7c2251cc2e588d5cd87bae567260617c6de0c22dca9cdbfc4c7d2b8990b62
ARG GIT_URL=https://mirrors.edge.kernel.org/pub/software/scm/git

# Build options mirror GIT-BUILD-OPTIONS exactly; the test suite reads them.
ENV GIT_MAKE_OPTS="NO_GETTEXT=1 NO_PERL_CPAN_FALLBACKS=1"

RUN set -eux; \
    for spec in 2.20.1:${GIT_2_20_1_SHA256} 2.39.5:${GIT_2_39_5_SHA256}; do \
        v="${spec%%:*}"; sum="${spec##*:}"; \
        curl -fsSL -o /tmp/git-$v.tar.xz "$GIT_URL/git-$v.tar.xz"; \
        echo "$sum  /tmp/git-$v.tar.xz" | sha256sum -c -; \
        mkdir -p /tmp/src-$v; \
        tar -xJf /tmp/git-$v.tar.xz -C /tmp/src-$v --strip-components=1; \
        make -C /tmp/src-$v -j"$(nproc)" prefix=/opt/gitpeer/v$v $GIT_MAKE_OPTS all; \
        make -C /tmp/src-$v prefix=/opt/gitpeer/v$v $GIT_MAKE_OPTS install; \
        rm -rf /tmp/src-$v /tmp/git-$v.tar.xz; \
    done

# 2.51.0 is also the source of the test suite, so its build tree is kept and
# built in place at its final path -- that is what makes GIT-BUILD-OPTIONS
# self-consistent without rewriting paths afterwards.
RUN set -eux; \
    v=2.51.0; \
    curl -fsSL -o /tmp/git-$v.tar.xz "$GIT_URL/git-$v.tar.xz"; \
    echo "${GIT_2_51_0_SHA256}  /tmp/git-$v.tar.xz" | sha256sum -c -; \
    mkdir -p /opt/git-suite; \
    tar -xJf /tmp/git-$v.tar.xz -C /opt/git-suite --strip-components=1; \
    cd /opt/git-suite; \
    make -j"$(nproc)" prefix=/opt/gitpeer/v$v $GIT_MAKE_OPTS all; \
    make prefix=/opt/gitpeer/v$v $GIT_MAKE_OPTS install; \
    make -j"$(nproc)" prefix=/opt/gitpeer/v$v $GIT_MAKE_OPTS t/helper/test-tool; \
    rm -f /tmp/git-$v.tar.xz

# Keep only what the suite needs; the rest of the build tree is source and
# object files the verifier must not ship.
RUN set -eux; \
    cd /opt/git-suite; \
    find . -mindepth 1 -maxdepth 1 \
        ! -name t ! -name templates ! -name Documentation ! -name GIT-BUILD-OPTIONS \
        -exec rm -rf {} +; \
    test -x /opt/git-suite/t/helper/test-tool; \
    test -d /opt/git-suite/templates/blt; \
    grep -q "^GIT_TEST_TEMPLATE_DIR='/opt/git-suite/templates/blt'" /opt/git-suite/GIT-BUILD-OPTIONS; \
    grep -q "^GIT_SOURCE_DIR='/opt/git-suite'" /opt/git-suite/GIT-BUILD-OPTIONS

# ---------------------------------------------------------------------------
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      bash ca-certificates coreutils curl diffutils file findutils gawk grep \
      less make patch perl procps python3 sed tar xz-utils unzip xxd \
      php8.2-cli \
      zlib1g libcurl4 libexpat1 gettext-base \
      openssh-server openssh-client apache2 \
      socat netcat-openbsd iproute2 \
 && rm -rf /var/lib/apt/lists/* \
 && a2enmod cgi alias env >/dev/null 2>&1 || true

COPY --from=gitbuild /opt/gitpeer /opt/gitpeer
COPY --from=gitbuild /opt/git-suite /opt/git-suite

# A missing peer or a missing test-tool would not fail a run -- it would score
# every candidate the same way a broken candidate scores.  Fail the build.
RUN /opt/gitpeer/v2.51.0/bin/git --version \
 && /opt/gitpeer/v2.39.5/bin/git --version \
 && /opt/gitpeer/v2.20.1/bin/git --version \
 && php -v | head -1 \
 && prove --version | head -1 \
 && test -x /opt/git-suite/t/helper/test-tool \
 && ! command -v gcc \
 && ! command -v git
# <<< self-contained base <<<

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends ripgrep vim-tiny strace time \
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

# A real git on PATH, for comparing behaviour while developing. It is not part
# of the graded artifact: /app is collected, /usr/local/bin and /opt are not.
RUN ln -sf /opt/gitpeer/v2.39.5/bin/git /usr/local/bin/git \
 && ln -sf /opt/gitpeer/v2.39.5/bin/git-upload-pack /usr/local/bin/git-upload-pack \
 && ln -sf /opt/gitpeer/v2.39.5/bin/git-receive-pack /usr/local/bin/git-receive-pack

WORKDIR /app

# Keep the script and every step's timeout_sec synchronized. The script is an
# agent-visible countdown; Harbor enforces the actual timeout.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh
