# Verifier image for one step, built with that step's tests/ directory as the
# Docker build context: it cannot reach the task root, environment/, or another
# step, so helpers.sh and the driver crate are copied into every step. Harbor
# restores the collected /app artifact into this image and runs /tests/test.sh.
FROM rust:1.88-bookworm

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates coreutils diffutils gawk pkg-config \
    && rm -rf /var/lib/apt/lists/*

# /app is repopulated from the collected artifact and /logs/verifier is bind
# mounted at run time; creating them keeps workdir valid at container start.
RUN mkdir -p /app /logs/verifier

# Step identity, for the binding check at the top of test.sh.
#
# Written BEFORE the COPY below, so it names this IMAGE even when the copy is
# served from another step's cache entry. The builder identifies a build-context
# file by (relative path, size, mtime) and never reads its bytes, so two steps
# whose tests/ trees share a filename, a size and a second are indistinguishable
# to it -- and the wrong milestone's verifier gets baked in at an ordinary-looking
# score. Nothing inside a Dockerfile prevents that; this only reports it.
#
# Written as a LITERAL rather than as an ARG: harbor builds the verifier
# through docker-compose-build.yaml, which declares no `args:`, so no
# --build-arg can reach this build and an ARG default would be an override
# surface nothing can use.
#
# Keep this number, the directory's number and test.sh's check equal.
RUN echo "02" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh

# Populate Cargo's registry cache for this step's exact dependency graph, so
# grading does not resolve a moving graph or depend on a slow mirror. The
# prefetch uses the pinned crate manifest for this step's release plus the
# driver crate's manifest and lockfile, and that is the resolution test.sh
# PREFERS for the drivers: it builds them `--offline --locked` against this
# cache, so for a candidate that left Cargo.toml alone - the Oracle included -
# the driver build resolves nothing and downloads nothing. test.sh falls back
# to a candidate-derived resolution only when the pinned one does not satisfy
# the candidate's own manifest, and never mixes the two; see "Choosing the
# drivers' dependency resolution" in test.sh.
RUN set -eux; \
    mkdir -p /prefetch/candidate/src /prefetch/drivers/tests; \
    cp /tests/assets/prefetch/candidate-Cargo.toml /prefetch/candidate/Cargo.toml; \
    : > /prefetch/candidate/src/lib.rs; \
    sed 's|path = "/app"|path = "../candidate"|' /tests/assets/harness/Cargo.toml \
        > /prefetch/drivers/Cargo.toml; \
    cp /tests/assets/harness/Cargo.lock.fallback /prefetch/drivers/Cargo.lock; \
    : > /prefetch/drivers/tests/conformance.rs; \
    : > /prefetch/drivers/tests/gate.rs; \
    cd /prefetch/drivers && cargo fetch --locked; \
    cd /; rm -rf /prefetch

ENV CARGO_TERM_COLOR=never
WORKDIR /app
