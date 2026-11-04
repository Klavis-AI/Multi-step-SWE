FROM debian:bookworm-slim

ARG ETCD_VERSION=v3.5.21
ARG KUBECTL_VERSION=v1.25.16

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates curl file gcc git gdb less libc6-dev libssl-dev \
       make openssl procps python3 ripgrep valgrind vim jq xz-utils \
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

# A real etcd: the control plane stores its objects in it, exactly as upstream does.
RUN curl -fsSL -o /tmp/etcd.tgz \
      "https://github.com/etcd-io/etcd/releases/download/${ETCD_VERSION}/etcd-${ETCD_VERSION}-linux-amd64.tar.gz" \
    && tar -xzf /tmp/etcd.tgz -C /usr/local/bin --strip-components=1 \
       "etcd-${ETCD_VERSION}-linux-amd64/etcd" "etcd-${ETCD_VERSION}-linux-amd64/etcdctl" \
    && rm -f /tmp/etcd.tgz

# Every release's own kubectl, so a workspace can be driven by the real client at
# whichever Kubernetes version it targets.  `kubectl` itself is the 1.25 client.
RUN set -eu; \
    for v in v1.25.16 v1.26.15 v1.27.16 v1.28.15 v1.29.15 \
             v1.30.14 v1.31.14 v1.32.13 v1.33.13; do \
      m="$(echo "$v" | cut -d. -f1-2)"; \
      curl -fsSL -o "/usr/local/bin/kubectl-${m#v}" \
        "https://dl.k8s.io/release/$v/bin/linux/amd64/kubectl"; \
      chmod +x "/usr/local/bin/kubectl-${m#v}"; \
    done; \
    cp /usr/local/bin/kubectl-1.25 /usr/local/bin/kubectl

# Build products go here, outside the workspace that is collected between
# the agent and the grader.
RUN mkdir -p /out/bin

WORKDIR /app

COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh

# Documentation for the agent, which works with no network: the specs and
# manuals the instructions cite, stored in environment/docs/. They were
# downloaded once from their official sources and checked against pinned
# sha256 sums; docs-provenance.txt records the URLs, sums and conversions.
# /opt/docs is outside /app on purpose: /app is the workspace collected for
# grading.
COPY docs/ /opt/docs/
