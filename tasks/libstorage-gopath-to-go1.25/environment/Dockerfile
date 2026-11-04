# Agent environment: the Go 1.25 toolchain and the libStorage tree as the
# project left it in 2017. The repository's own tests are unchanged, and no
# older toolchain is installed: GOTOOLCHAIN=local means the pinned compiler is
# the only one that can ever run.
FROM golang:1.25.14-bookworm

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates curl git jq less openssl procps python3 ripgrep \
       tree vim-tiny xz-utils \
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

# Module resolution for this environment. GOPROXY points at a self-contained
# mirror that publishes each of the project's pinned dependencies at exactly
# one version, so minimal version selection has nothing to choose and the
# module graph reproduces the code the GOPATH build used. GO111MODULE=auto
# leaves the inherited tree in GOPATH mode (it has no go.mod) and switches to
# module mode by itself once one exists.
ENV GOPATH=/go \
    GOTOOLCHAIN=local \
    GO111MODULE=auto \
    GOPROXY=file:///opt/gomodmirror \
    GOSUMDB=off \
    GOPRIVATE= \
    GONOSUMDB=

# A login shell sources /etc/profile, which replaces PATH and would hide the
# toolchain the base image put there.
RUN printf 'export PATH=$PATH:/usr/local/go/bin:/go/bin\n' > /etc/profile.d/go.sh

COPY gomodmirror /opt/gomodmirror
COPY warm_module_cache.sh /usr/local/bin/warm_module_cache.sh
# The mirror arrives from the host with root-only subdirectories, so
# GOPROXY=file:///opt/gomodmirror is unreadable to `builder` -- the user the
# test suite has to run as -- and every fetch fails `permission denied` on the
# .zip while the warm cache hides it for anything already downloaded. Both
# trees are made world-readable: the mirror so a fetch can happen at all, the
# cache so it usually does not have to.
RUN chmod +x /usr/local/bin/warm_module_cache.sh \
    && /usr/local/bin/warm_module_cache.sh \
    && chmod -R a+rX /go/pkg/mod \
    && chmod -R a+rX /opt/gomodmirror

# The repository. /app is the tree; because it is a GOPATH-era project it is
# also reachable at its import path inside GOPATH, which is where its own
# build and test commands work before it becomes a module.
COPY libstorage /app
RUN mkdir -p /go/src/github.com/codedellemc \
    && ln -s /app /go/src/github.com/codedellemc/libstorage

# Agent-visible countdown, at the path every step instruction names. It is a
# workspace file rather than a project file: nothing in the repository reads it,
# and no gate looks at anything but Go packages.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh

# The repository's own suite must not run as root: libStorage resolves its
# default paths differently for uid 0. A non-root account is present for that.
RUN useradd -m -u 1001 -s /bin/bash builder \
    && chown -R 1001:1001 /app \
    && mkdir -p /go/pkg/mod/cache && chmod -R a+rwX /go/pkg

WORKDIR /app
