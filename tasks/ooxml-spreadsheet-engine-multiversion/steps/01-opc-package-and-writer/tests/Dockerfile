# Verifier image for milestone 01 (01-opc-package-and-writer), built with
# steps/01-opc-package-and-writer/tests/ as the Docker build context: it cannot reach the task
# root, environment/, or another step, so helpers.sh, the driver packages and
# the pinned dependency set are copied into every step. Harbor skips its usual
# tests upload here, restores the cumulative /app artifact into this image and
# runs /tests/test.sh.
#
# Pinned by digest, not by tag: grading must not change under a step that only
# re-runs `docker build`. The digest names the multi-architecture index for
# golang:1.25-bookworm.
FROM golang:1.25-bookworm@sha256:3b4a11519ad929d1e1d261a12cff056f0c85b735253d7d861346b9c6f8b36437

# The verifier owns its toolchain independently of environment/Dockerfile, so a
# candidate that modified its own workspace cannot change how it is graded.
#
# Nothing is installed: every tool the harness invokes already ships in the
# pinned base image, so this image BUILDS offline as well as grading offline.
# `apt-get update` would otherwise resolve against whatever the Debian pool
# holds on the day of the build and make the image irreproducible. The list is
# asserted rather than installed, so a base image that ever stopped shipping
# one of them fails the build loudly instead of failing a scored group
# silently. (`awk` here is mawk; every awk expression in helpers.sh and test.sh
# is POSIX and was checked against it.)
RUN set -eu; \
    for t in bash sh awk sed grep basename dirname cat cp rm mkdir mktemp \
             find sort xargs head tail wc tr cmp diff sha256sum timeout \
             chmod chown id setpriv ls printf go; do \
        command -v "$t" > /dev/null || { echo "verifier image lacks $t" >&2; exit 1; }; \
    done

# Grading is fully offline and hermetic: GOPROXY=off makes any attempt to
# resolve a module a hard error instead of a network fetch, GOTOOLCHAIN=local
# stops the go command downloading a toolchain named by a candidate-edited
# go.mod, and GOFLAGS drops the VCS stamp so /app need not be a git checkout.
ENV GOTOOLCHAIN=local \
    GOPROXY=off \
    GOFLAGS=-buildvcs=false \
    CGO_ENABLED=0 \
    LC_ALL=C \
    LANG=C

# Warm a read-only build cache for the standard library. Go 1.20 and later ship
# no precompiled stdlib, so without this every scored group would pay the same
# ~30 s rebuild. test.sh seeds the unprivileged GOCACHE from this copy; it is
# build output only and contains no part of the solution.
RUN set -eu; \
    GOCACHE=/opt/gocache go build std; \
    chmod -R a+rX /opt/gocache

# /app is repopulated from the collected artifact and /logs/verifier is bind-
# mounted at run time; creating them keeps workdir valid at container start.
RUN mkdir -p /app /logs/verifier

# Step identity, for the `require_step` check at the top of test.sh.
#
# This does NOT prevent the wrong tests/ tree being baked in -- nothing inside
# a Dockerfile can. The builder keys its content checksum for a local build
# context on (path, size, mtime) and never reads the bytes, so two steps whose
# tests/ trees are staged at the same path with the same sizes and the same
# mtime resolve to the same copy, however different their contents. Only a
# distinct mtime (or size) per step prevents it, which is what
# bin/stamp-steps.py is for and what bin/check-binding.sh proves.
#
# What this line buys is *detection*. /etc/harbor-step-id is written before the
# copy below, so it names the image; test.sh's `require_step` names the tree.
# When the two disagree the verifier says so, instead of reporting an
# ordinary-looking partial score.
#
# Keep this number, the directory's number and test.sh's `require_step` equal.
# Written as a LITERAL, substituted when this file is generated, not as an ARG:
# harbor builds the verifier through docker-compose-build.yaml, which declares
# no `args:`, so no --build-arg can reach this build and an ARG default would be
# an override surface nothing can use.
RUN echo "01" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh

WORKDIR /app
