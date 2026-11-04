# Verifier image for one step, built with that step's tests/ directory as the
# Docker build context: it cannot reach the task root, environment/, or another
# step, so helpers.sh and the driver crate are copied into every step. Harbor
# restores the collected /app artifact into this image and runs /tests/test.sh.
FROM rust:1.98-bookworm

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates coreutils diffutils gawk pkg-config \
    && rm -rf /var/lib/apt/lists/*

# /app is repopulated from the collected artifact and /logs/verifier is bind
# mounted at run time; creating them keeps workdir valid at container start.
RUN mkdir -p /app /logs/verifier

# The step this IMAGE was built for, baked in BEFORE the build context is
# copied, so it names the image rather than the tree. test.sh's `require_step`
# compares it with the step the /tests TREE claims to be. If the builder ever
# serves this image another step's context, the two disagree and the verifier
# says so instead of grading the wrong milestone at a plausible-looking score.
#
# The DEFAULT is rendered per step, it is not a fixed literal: harbor builds
# this image through a compose file that declares a build context and no
# `args:` (harbor/environments/docker/docker-compose-build.yaml), so nothing
# passes --build-arg and the default is what lands in the image. A fixed
# `ARG STEP_ID=01` would give every step's image step 01 and make steps 02-11
# trip the mismatch against a perfectly healthy tree.
#
# It is kept as an ARG rather than inlined so that the day harbor DOES pass
# --build-arg STEP_ID=<step directory number> from outside the build context,
# this becomes a genuinely independent identity with no edit here. Today both
# identities are rendered into tests/, so they are independent only because
# BuildKit's staleness is per file.
ARG STEP_ID=01
RUN echo "$STEP_ID" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh

# Populate Cargo's registry cache for this step's exact dependency graph, so
# grading does not resolve a moving graph or depend on a slow mirror. The
# prefetch uses the pinned crate manifests for this step's release plus the
# driver crate's manifest and lockfile, and that is the resolution test.sh
# PREFERS for the drivers: it builds them `--offline --locked` against this
# cache, so for a candidate that left the manifests alone - the Oracle included
# - the driver build resolves nothing and downloads nothing.
RUN set -eux; \
    mkdir -p /prefetch/drivers/tests /prefetch/drivers/src; \
    tar -xzf /tests/assets/prefetch/candidate-workspace.tar.gz -C /prefetch; \
    sed 's|path = "/app/icechunk"|path = "../candidate/icechunk"|' \
        /tests/assets/harness/Cargo.toml > /prefetch/drivers/Cargo.toml; \
    cp /tests/assets/harness/Cargo.lock.fallback /prefetch/drivers/Cargo.lock; \
    : > /prefetch/drivers/tests/conformance.rs; \
    : > /prefetch/drivers/tests/gate.rs; \
    : > /prefetch/drivers/src/lib.rs; \
    cd /prefetch/candidate && cargo fetch --locked; \
    cd /prefetch/drivers && cargo fetch --locked; \
    cd /; rm -rf /prefetch

ENV CARGO_TERM_COLOR=never
WORKDIR /app
