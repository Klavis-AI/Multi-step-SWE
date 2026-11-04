# Agent environment.  Node.js is the implementation toolchain; nothing here
# implements, parses, shapes, rasterizes or subsets a font.  In particular there
# is deliberately no FreeType, no HarfBuzz, no fontTools and no OTS: those are
# the reference implementations this milestone is graded against and they live
# only in the verifier image, which the agent never sees.  /opt/docs holds the
# specification chapters and registries /data/spec does not carry (see
# docs-manifest.txt); documents only, no code.


FROM node:22-bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash ca-certificates coreutils curl file findutils gawk git less \
        procps ripgrep unzip vim xxd xz-utils \
    && rm -rf /var/lib/apt/lists/*

# /app is the cumulative source workspace that travels between steps and is the
# only thing collected for grading.  /data holds each step's read-only payload.
RUN mkdir -p /app /data
WORKDIR /data

COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh

# Documentation for the agent, which works with no network: the specs and
# manuals the instructions cite, stored in environment/docs/. They were
# downloaded once from their official sources and checked against pinned
# sha256 sums; docs-provenance.txt records the URLs, sums and conversions.
# /opt/docs is outside /app on purpose: /app is the workspace collected for
# grading.
COPY docs/ /opt/docs/
