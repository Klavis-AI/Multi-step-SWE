# Agent environment for evm-state-transition-multiversion.
#
# The candidate builds a go-ethereum working tree, so this image carries a
# pinned Go toolchain rather than Debian's. Every step's verifier installs its
# own copy of the same toolchain from its own Dockerfile: nothing is inherited
# from this image at grading time.
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
         bash build-essential ca-certificates coreutils curl diffutils gawk \
         git jq less patch procps python3 ripgrep xz-utils \
    && rm -rf /var/lib/apt/lists/*

# Pinned toolchain. GOTOOLCHAIN=local keeps a `go` directive in a candidate's
# go.mod from silently downloading a different compiler mid-build.
ARG GO_VERSION=1.25.1
ARG GO_SHA256=7716a0d940a0f6ae8e1f3b3f4f36299dc53e31b16840dbd171254312c41ca12e
RUN curl -fsSL -o /tmp/go.tgz "https://go.dev/dl/go${GO_VERSION}.linux-amd64.tar.gz" \
    && echo "${GO_SHA256}  /tmp/go.tgz" | sha256sum -c - \
    && tar -C /usr/local -xzf /tmp/go.tgz \
    && rm /tmp/go.tgz
ENV PATH=/usr/local/go/bin:/root/go/bin:$PATH \
    GOTOOLCHAIN=local \
    GOFLAGS=-mod=mod \
    GOMAXPROCS=4 \
    GOPATH=/root/go \
    GOCACHE=/root/.cache/go-build

WORKDIR /app

# The workspace seed: go-ethereum at v1.10.0, the last release before London.
# Pinned by tag and re-checked by commit hash, because a tag can be moved.
#
# .git is removed on purpose. task.toml excludes it from the artifact, so it
# would not survive the first checkpoint anyway, and a tree that has history at
# step 01 and none at step 02 is a difference the candidate has to discover.
# Every step now sees the same kind of thing: a plain source tree.
ARG GETH_TAG=v1.10.0
ARG GETH_COMMIT=56dec25ae26bf749b93c3ea69538fabea60c5768
RUN git clone --depth 1 --branch "$GETH_TAG" https://github.com/ethereum/go-ethereum /tmp/geth \
    && test "$(git -C /tmp/geth rev-parse HEAD)" = "$GETH_COMMIT" \
    && rm -rf /tmp/geth/.git \
    && cp -a /tmp/geth/. /app/ \
    && rm -rf /tmp/geth

# The upstream pin of github.com/fjl/memsize uses //go:linkname against
# runtime.stopTheWorld, whose signature changed; it no longer links under a
# modern toolchain, and internal/debug is on cmd/evm's import path. Without
# this the seeded tree would not build at all, and WORKSPACE_HANDOFF.md's
# promise that it does would be false. The stub reproduces the API surface and
# nothing else -- memsize is a debug HTTP handler, irrelevant to grading.
COPY memsize-stub /app/third_party/memsize-stub
RUN cd /app && go mod edit -replace github.com/fjl/memsize=./third_party/memsize-stub

# Warm the module and build caches so the candidate's first build is not spent
# downloading. This also proves the seeded tree compiles as shipped.
RUN cd /app && go mod download && go build -p 4 -o /app/build/t8n ./cmd/evm \
    && /app/build/t8n t8n --help > /dev/null && rm -rf /app/build

# The EIP-4844 point-evaluation precompile needs a KZG verifier *and the trusted
# setup that verifier checks against*. The setup is data -- 447 KB of ceremony
# output in /app/docs/fork/cancun/eip-4844.md's own terms -- so it cannot be
# derived, and this image reaches only the model's inference endpoint: a
# candidate who follows the rules cannot fetch it. TEST_INTERFACE.md therefore
# names the two KZG modules that resolve, at the versions the ladder's releases
# use, and they are warmed here because the seed's own module graph does not
# contain them (measured on the 2026-10-09 rollout: the step-05 agent checked
# its cache, found crate-crypto/go-kzg-4844 absent, and had to reach the
# network, which its instructions say it does not have).
#
# `go mod download module@version` writes the module -- zip *and* extracted
# source, which is where the trusted setup lives -- into the cache without
# touching the seeded go.mod or go.sum; the cmp below makes that a build-time
# check rather than a claim.
RUN cd /app \
    && cp go.mod /tmp/go.mod.seed && cp go.sum /tmp/go.sum.seed \
    && go mod download \
         github.com/crate-crypto/go-kzg-4844@v1.0.0 \
         github.com/ethereum/c-kzg-4844@v1.0.0 \
    && cmp go.mod /tmp/go.mod.seed && cmp go.sum /tmp/go.sum.seed \
    && rm -f /tmp/go.mod.seed /tmp/go.sum.seed

# Agent-visible countdown. Harbor's steps.agent.timeout_sec is the enforcing
# limit; keep the two synchronized.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh
