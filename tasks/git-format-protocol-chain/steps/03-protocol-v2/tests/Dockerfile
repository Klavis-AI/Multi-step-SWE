# Verifier image. Build context is a step's tests/ directory.
# No C compiler and no git on PATH: the candidate is rebuilt from its own PHP
# sources and graded only through its own binary and the pinned real gits.

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

# Everything that runs candidate-supplied code -- the build included -- runs as
# this user.  `make` is the most privileged process in a verifier and does not
# read like a launcher, so a recipe would otherwise be able to rewrite the
# scorer and the expectations it is scored against.
RUN useradd -m -u 2000 -s /bin/bash grader

RUN mkdir -p /app /logs/verifier && chown grader /app

COPY . /tests
RUN find /tests -name __pycache__ -type d -prune -exec rm -rf {} + \
 && touch /tests/.grader-required \
 && chmod +x /tests/test.sh && chmod -R go-rwx /tests

WORKDIR /app
