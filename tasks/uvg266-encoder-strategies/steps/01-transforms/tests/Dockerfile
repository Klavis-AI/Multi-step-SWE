# Verifier image for one step, built with that step's tests/ as the Docker
# build context. It reconstructs THIS STEP'S BASE from the pinned upstream
# commit, builds it, and keeps it as the reference: the per-function speedup
# denominator is measured here, in the same container and on the same pinned
# cores as the candidate, so the ratio is self-calibrating and does not depend
# on how fast the grading machine is.
#
# This file is identical for every step. What differs is tests/BASE_STAGES.stepNN
# (which stages this step's base has removed) and the other per-step data files.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash bc ca-certificates cmake coreutils diffutils g++ gcc gawk git make \
       patch procps python3 time util-linux \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /logs/verifier /workload

# Step identity, for the binding check at the top of test.sh.
#
# Written BEFORE the COPY below, so it names this IMAGE even when the copy is
# served from another step's cache entry. The builder identifies a build-context
# file by (relative path, size, mtime) and never reads its bytes, so two steps
# whose tests/ trees share a filename, a size and a second are indistinguishable
# to it -- and the wrong milestone's verifier gets baked in at an ordinary-looking
# score. Nothing inside a Dockerfile prevents that; this only reports it.
#
# Written as a LITERAL rather than as
# an ARG: the runner builds the verifier through docker-compose-build.yaml, which
# declares no `args:`, so no --build-arg can reach this build.
#
# Keep this number, the directory's number and test.sh's check equal.
RUN echo "01" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh

# This step's base tree, and a pristine copy of the pin for the similarity
# diagnostic. Both come from the same clone, so the diagnostic never depends on
# the network at grading time.
#
# BASE_STAGES names the *ladder* stages this step's base still has removed.
# `alf` and `depquant` are not ladder stages and are removed from every level
# including the base: the agent's tree never carries either family, so a base
# that kept their optimized twins would make the end-to-end bitstream
# comparison pit a base that can install upstream's alf twin against a
# candidate that only ever has the generic -- two different programs.  The
# alf twin is documented as not bit-exact with the generic (README.md), so
# that comparison is not a correctness gate at all.
RUN . /tests/vendor/PIN \
    && git clone --quiet "$UVG_REPO" /base \
    && cd /base \
    && git checkout --quiet "$UVG_SHA" \
    && test "$(git rev-parse HEAD)" = "$UVG_SHA" \
    && git submodule update --init --quiet greatest \
    && rm -rf .git .gitmodules \
    && cp -a /base /upstream \
    && python3 /tests/vendor/strip.py /base alf depquant $(cat /tests/BASE_STAGES.step01)

RUN cmake -S /base -B /basebld -DCMAKE_BUILD_TYPE=Release -DUVG_BUILD_TESTS=ON > /dev/null \
    && make -C /basebld -j"$(nproc)" > /dev/null \
    && test -x /basebld/bin/uvg266

# The end-to-end workload, regenerated here rather than read from /app: the
# candidate controlled /app for the whole step and must not choose its own input.
RUN python3 /tests/vendor/make_clip.py /workload/clip.yuv 176 144 6

WORKDIR /app
