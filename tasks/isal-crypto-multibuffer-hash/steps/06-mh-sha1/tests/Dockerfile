# Verifier image for one step, built with that step's tests/ as the Docker
# build context. It reconstructs THE TASK'S BASE -- the pinned upstream tree
# with every ladder stage's optimized implementation removed -- builds it, and
# keeps it as the speedup denominator: the base is measured here, in the same
# container and on the same pinned cores as the candidate, so the ratio is
# self-calibrating and does not depend on how fast the grading machine is.
#
# The base is the same tree at every step, so this file is identical in every
# step apart from the step-id literal near the bottom. What differs per step is
# tests/FUNCS.stepNN, tests/STAGE.stepNN, tests/harness/bench.stepNN.c and
# tests/BINDING.stepNN.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates coreutils libc6-dev diffutils gawk gcc git libssl-dev make \
       nasm patch procps python3 time util-linux \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /logs/verifier

# --- shared layer: identical in every step, so BuildKit caches it after the
# first image and only the per-step layers below are rebuilt. Placed before the
# step-id literal on purpose: a per-step instruction above it would change the
# cache key of everything after it and this clone/build would run seven times.
COPY vendor/ /tmp/vendor/
RUN bash /tmp/vendor/build_base.sh

# --- end shared layer ------------------------------------------------------

# Step identity. Written as a per-step literal rather than as an ARG: harbor
# builds the verifier through docker-compose-build.yaml, which declares no
# `args:`, so no --build-arg can reach this build and an ARG default would be
# an override surface nothing can use. Keep this number, the directory's
# number and test.sh's expectations equal.
RUN echo "06" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh

WORKDIR /app
