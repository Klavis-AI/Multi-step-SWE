# Verifier image for step 01, built with steps/01-generator-and-runtime/tests/ as the
# Docker build context: it cannot reach the task root or another step, so every
# shared file (helpers.sh, driver.py, score.py, the fixture corpus) is copied
# into each step's tests/ tree.
#
# The toolchain here is the verifier's own, installed independently of
# environment/Dockerfile, so grading never depends on a toolchain the candidate
# could have modified. It has to be able to BUILD the candidate -- the contract
# in TEST_INTERFACE.md is an executable /app/build.sh producing
# /app/bin/tree-sitter -- so it carries a full Rust and C toolchain plus node,
# which older tree-sitter releases shell out to when evaluating grammar.js.
FROM node:22-bookworm-slim AS nodesrc

FROM rust:1.98-bookworm

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash ca-certificates coreutils diffutils gawk util-linux \
        python3 git clang libclang-dev pkg-config \
    && rm -rf /var/lib/apt/lists/*

# node 22, for `generate` on a grammar.js. Releases before v0.25.0 require an
# external node; later ones embed a JS engine and ignore it.
COPY --from=nodesrc /usr/local/bin/node /usr/local/bin/node

# Bake the union of every release anchor's locked dependency graph into the
# image, so grading resolves a candidate build without a live network. The
# shipped `prewarm/` tree is only manifests and empty target stubs (~2 MB); the
# crates themselves are fetched here, at image build time, when the network is
# available. Nothing in prewarm/ is ever compiled or linked, and none of it is
# on any path the candidate or the checks read.
ENV CARGO_HOME=/opt/cargo
COPY prewarm /opt/prewarm
RUN set -eu; \
    for d in /opt/prewarm/*/; do \
        ( cd "$d" && cargo fetch --locked ) || \
            echo "prewarm: $d did not resolve; grading falls back to the network" >&2; \
    done; \
    rm -rf /opt/prewarm; \
    chmod -R 0777 /opt/cargo

# /app is repopulated from the collected artifact and /logs/verifier is bind-
# mounted at run time; creating them keeps workdir valid at container start.
RUN mkdir -p /app /logs/verifier

# Step identity for `require_step`, written BEFORE the copy below so it names
# the image while test.sh's require_step names the tree; a build cache that
# serves another step's tests/ then shows up as a mismatch. Literal, not an
# ARG: harbor builds through docker-compose-build.yaml, which declares no
# `args:`.
RUN echo "01" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh

WORKDIR /app
