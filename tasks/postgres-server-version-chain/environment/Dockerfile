# Agent environment: a C toolchain and the upstream PostgreSQL *client*
# programs, so the candidate can drive its own server exactly the way the
# tests will.  There is deliberately no PostgreSQL *server* in this image.
FROM debian:bookworm-slim
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      bash ca-certificates curl gnupg git procps ripgrep less vim-tiny \
      build-essential gdb valgrind pkg-config make \
      python3 python3-pip diffutils netcat-openbsd locales tzdata file strace \
 && install -d /usr/share/postgresql-common/pgdg \
 && curl -fsSL -o /usr/share/postgresql-common/pgdg/apt.postgresql.org.asc \
      https://www.postgresql.org/media/keys/ACCC4CF8.asc \
 && echo "deb [signed-by=/usr/share/postgresql-common/pgdg/apt.postgresql.org.asc] https://apt.postgresql.org/pub/repos/apt bookworm-pgdg main 18" \
      > /etc/apt/sources.list.d/pgdg.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends postgresql-client-18 \
 && rm -rf /var/lib/apt/lists/* \
 && rm -f /usr/lib/postgresql/*/bin/postgres /usr/lib/postgresql/*/bin/postmaster \
          /usr/lib/postgresql/*/bin/initdb /usr/lib/postgresql/*/bin/pg_ctl 2>/dev/null || true \
 && rm -f /etc/apt/sources.list.d/pgdg.list \
          /usr/share/postgresql-common/pgdg/apt.postgresql.org.asc

# Agent harnesses need node and npm on PATH; without them the runtime's setup runs
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

RUN pip3 install --break-system-packages --no-cache-dir "psycopg[binary]==3.2.3"
ENV PATH=/usr/lib/postgresql/18/bin:$PATH
ENV TZ=UTC PGTZ=UTC LANG=C LC_ALL=C
WORKDIR /app
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh

# Documentation for the agent, which works with no network: the specs and
# manuals the instructions cite, stored in environment/docs/. They were
# downloaded once from their official sources and checked against pinned
# sha256 sums; docs-provenance.txt records the URLs, sums and conversions.
# /opt/docs is outside /app on purpose: /app is the workspace collected at the
# end of the step.
COPY docs/ /opt/docs/
