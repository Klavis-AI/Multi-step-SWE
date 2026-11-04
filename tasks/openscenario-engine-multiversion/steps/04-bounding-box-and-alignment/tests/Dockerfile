# Verifier image for ONE step, built with that step's tests/ directory as the
# Docker build context: it cannot reach the task root, environment/, or another
# step, so helpers.sh, caselib.sh and the case corpus are copied into every
# step. Harbor restores the collected /app artifact into this image and runs
# /tests/test.sh.
#
# The toolchain here must match environment/Dockerfile's, because this image is
# where the candidate's source is actually compiled.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash build-essential ca-certificates cmake coreutils diffutils file \
       gawk git make patch pkg-config procps python3 xz-utils \
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
# Written as a LITERAL, substituted when this file is generated, rather than as
# an ARG: harbor builds the verifier through docker-compose-build.yaml, which
# declares no `args:`, so no --build-arg can reach this build and an ARG default
# would be an override surface nothing can use.
#
# Keep this number, the directory's number and test.sh's check equal.
RUN echo "04" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh /tests/caselib.sh \
    && find /tests/assets/cases -name '*.sh' -exec chmod +x {} +

WORKDIR /app
