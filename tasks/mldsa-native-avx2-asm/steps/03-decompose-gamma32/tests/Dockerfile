# Verifier image for step 03, built with steps/03-decompose-gamma32/tests/ as the
# Docker build context. It reconstructs THIS STEP'S reference tree from the
# pinned upstream commit, builds it, records which of the project's own suites
# that tree already fails, and bakes the reference measurement harnesses. The
# speedup denominator is therefore measured here, in the same container and on
# the same pinned core as the candidate, so the ratio is self-calibrating and
# does not depend on how fast the grading machine is.
#
# Identical for every step. What differs is the step literal below, the digest
# block below it, and the per-step data files FUNCS.step03,
# BASE_STAGES.step03, ISA_MARKERS.step03 and SCOPE.step03.
FROM debian:bookworm-slim

# The verifier's toolchain is installed here and nowhere else: it must not be
# something the candidate could have changed in /app.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates coreutils diffutils g++ gcc gawk git make patch \
       procps python3 time util-linux \
    && rm -rf /var/lib/apt/lists/*

# /app is repopulated from the collected artifact and /logs/verifier is bind-
# mounted at run time; creating them keeps workdir valid at container start.
RUN mkdir -p /app /logs/verifier

# Step identity, for the `require_step` check at the top of test.sh. Written
# BEFORE the copy below, so it names this IMAGE even when the copy is served
# from another step's cache entry. The builder identifies a build-context file
# by (relative path, size, mtime) and never reads its bytes, so two steps whose
# data files share a path and a size are indistinguishable to it -- and the
# wrong milestone's verifier gets baked in at an ordinary-looking score.
# Nothing inside a Dockerfile prevents that; this only reports it.
#
# Written as a LITERAL, substituted when this file is generated, rather than as
# an ARG: harbor builds the verifier through docker-compose-build.yaml, which
# declares no `args:`, so no --build-arg can reach this build.
RUN echo "03" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh /tests/vendor/prepare_base.sh

# Bind the per-step data files to this image. The digests are literals in this
# file, which is the build recipe and not part of the substituted context, so a
# data file from another step fails the build loudly instead of grading a
# different scope at a plausible-looking score.
RUN rm -f /tests/DIGESTS \
        && echo "d1b481bfb72224b2c3ee1c260c122149  BASE_STAGES.step03" >> /tests/DIGESTS \
    && echo "853e645523a35994b63e514cbc67120e  FUNCS.step03" >> /tests/DIGESTS \
    && echo "9ebe1a6af41c032d3f8de4a6d532d48b  ISA_MARKERS.step03" >> /tests/DIGESTS \
    && echo "c5c9150f50114c046946a6ae3d9a254f  SCOPE.step03" >> /tests/DIGESTS \
    && cd /tests && md5sum -c --quiet DIGESTS \
    && echo "bound $(grep -c . DIGESTS) data file(s)"

# This step's reference tree, its suites' baseline failures, and the reference
# harnesses. Fails the build if the reference does not build or run.
RUN bash /tests/vendor/prepare_base.sh

WORKDIR /app
