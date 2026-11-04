# Verifier image for step 03, built with steps/03-the-merge/tests/ as the Docker
# build context: it cannot reach the task root, environment/ or another step, so
# shared helpers are copied into each step.
#
# The verifier must not depend on a toolchain the candidate could have modified,
# so it installs its own pinned Go and its own tooling rather than inheriting
# anything from the agent image.
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
         bash build-essential ca-certificates coreutils curl diffutils gawk \
         git jq python3 ripgrep util-linux xz-utils \
    && rm -rf /var/lib/apt/lists/*

ARG GO_VERSION=1.25.1
ARG GO_SHA256=7716a0d940a0f6ae8e1f3b3f4f36299dc53e31b16840dbd171254312c41ca12e
RUN curl -fsSL -o /tmp/go.tgz "https://go.dev/dl/go${GO_VERSION}.linux-amd64.tar.gz" \
    && echo "${GO_SHA256}  /tmp/go.tgz" | sha256sum -c - \
    && tar -C /usr/local -xzf /tmp/go.tgz \
    && rm /tmp/go.tgz
# The caches live under /opt, not under /root, and they are world-writable.
# The candidate's build runs as nobody (see helpers.sh: candidate code must not
# be able to reach the reward file), and nobody cannot read /root. With the
# prewarmed cache in root's GOPATH every offline build fails with "module
# lookup disabled by GOPROXY=off" and the network fallback fails with
# "could not create module cache: permission denied" -- a correct candidate
# scoring 0 for a reason that has nothing to do with the milestone.
ENV PATH=/usr/local/go/bin:$PATH \
    GOTOOLCHAIN=local \
    GOFLAGS=-mod=mod \
    GOMAXPROCS=4 \
    GOPATH=/opt/go \
    GOMODCACHE=/opt/go/pkg/mod \
    GOCACHE=/opt/gocache

# The upstream pin of github.com/fjl/memsize uses //go:linkname against
# runtime.stopTheWorld, whose signature changed; it no longer links under a
# modern toolchain. internal/debug is on cmd/evm's import path, so without this
# stub nothing in the ladder builds at all. Copied before the prewarm so the
# prewarm below can be a real build.
COPY memsize-stub /opt/memsize-stub

# Prewarm at image build with three trees: the seed every step's workspace
# descends from (v1.10.0), the baseline the ladder starts this step from, and
# the anchor it targets. The seed is required, not decorative: the candidate's
# delivered tree keeps v1.10.0's go.mod pins, so a cache warmed only from the
# ladder's releases cannot resolve its module graph offline and the first two
# build attempts fail (measured on the 2026-10-09 rollout, steps 02-07). `go
# mod download` alone only fills the module graph; building ./cmd/evm is what
# proves the cache is complete, so a failure here surfaces at image build
# instead of as a candidate scoring 0. It also leaves GOCACHE warm, which is
# most of the candidate's build time.
RUN for tag in v1.10.0 v1.10.13 v1.10.23; do \
        git clone --depth 1 --branch "$tag" https://github.com/ethereum/go-ethereum /tmp/w \
        && cd /tmp/w \
        && go mod edit -replace github.com/fjl/memsize=/opt/memsize-stub \
        && go mod download \
        && go build -p 4 -o /tmp/prewarm-evm ./cmd/evm \
        && cd /tmp && rm -rf /tmp/w /tmp/prewarm-evm; \
    done

# Opened after the prewarm fills them, so the unprivileged build can read the
# module cache and write the build cache.
RUN chmod -R a+rwX /opt/go /opt/gocache

RUN mkdir -p /app /logs/verifier

# Step identity for the `require_step` check at the top of test.sh. This does
# NOT prevent the wrong tests/ tree being baked in -- the builder keys its
# content checksum on (path, size, mtime) and never reads the bytes. What it
# buys is detection: /etc/harbor-step-id is written before the COPY below, so
# it names the image, and test.sh's `require_step` names the tree. When they
# disagree the verifier says so instead of reporting an ordinary-looking
# partial score. bin/stamp-steps.py gives each step's tests/ a distinct mtime
# so the mis-binding does not happen in the first place.
#
# Keep this number, the directory's number and test.sh's `require_step` equal.
ARG STEP_ID=03
RUN echo "$STEP_ID" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh /tests/run_group.sh

WORKDIR /app
