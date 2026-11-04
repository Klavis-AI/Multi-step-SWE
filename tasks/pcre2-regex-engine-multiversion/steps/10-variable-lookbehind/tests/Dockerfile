# Verifier image for step 10, built with steps/10-variable-lookbehind/tests/ as the
# Docker build context: it cannot reach the task root or another step, so the
# shared helpers are copied into each step. Harbor skips its usual tests upload
# here and runs /tests/test.sh from this image.
FROM debian:bookworm-slim

# Grading rebuilds the candidate tree from source, so the toolchain lives here
# and not in environment/Dockerfile: the verifier must not depend on a compiler
# the candidate could have modified. Everything else is the comparison surface.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash binutils ca-certificates coreutils diffutils findutils gawk gcc \
        libc6-dev make sed util-linux \
    && rm -rf /var/lib/apt/lists/*

# /app is repopulated from the collected artifact and /logs/verifier is bind-
# mounted at run time; creating them keeps workdir valid at container start.
RUN mkdir -p /app /logs/verifier

# Step identity, for the `require_step` check at the top of test.sh.
#
# This does NOT prevent the wrong tests/ tree being baked in -- nothing inside
# a Dockerfile can. The builder keys its content checksum for a local build
# context on (path, size, mtime) and never reads the bytes, so two steps whose
# tests/ trees are staged at the same path with the same sizes and the same
# mtime resolve to the same copy, however different their contents. Neither
# --no-cache, nor a cache-busting ARG, nor a uniquely-named marker file helps;
# only a distinct mtime or a distinct size does.
#
# What this line buys is *detection*. /etc/harbor-step-id is written before the
# copy below, so it names the image; test.sh's `require_step` names the tree.
# When the two disagree the verifier says so, instead of reporting an
# ordinary-looking partial score.
#
# Written as a literal rather than an ARG: harbor builds the verifier through
# docker-compose-build.yaml, which declares no `args:`, so no --build-arg can
# reach this build.
RUN echo "10" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh

WORKDIR /app
