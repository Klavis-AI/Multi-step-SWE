# Agent image: Node.js and nothing that can decode H.264.
#
# Deliberately absent: ffmpeg/libav, gcc/g++/clang, python3, and any media
# library. The decoder under construction is JavaScript run by node, so a
# toolchain that could build or host a foreign decoder has no legitimate use
# here (no oracle or reference implementation in the agent's environment).
# PINNED BY DIGEST. Every property the capability layer relies on is a Node
# INTERNALS property -- undici's llhttp shape, the CJS named-export lexer, the
# builtin id table, test-runner flag propagation. A floating tag means a routine
# base refresh could invalidate every measurement recorded for this task, with
# nothing here noticing. This digest is node 22.23.2 / undici 6.28.0, which is
# what the gate was measured against.
FROM node:22-bookworm-slim@sha256:83f487e0a63425e5b4d146fb5e5be574bcbe1b7b843d3ebafdd95eaf7767a7e5

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
         bash ca-certificates coreutils curl git less procps ripgrep xxd \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Agent-visible countdown. Harbor's steps.agent.timeout_sec is authoritative.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh

# Documentation for the agent, which works with no network: the specs and
# manuals the instructions cite, stored in environment/docs/ and copied
# directly into the image.
# /opt/docs is outside /app because /app is the workspace collected for
# grading.
COPY docs/ /opt/docs/
