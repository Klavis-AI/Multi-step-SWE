# Verifier image for one step, built with that step's tests/ as the Docker build
# context. It reconstructs this step's base from the pinned upstream tag, builds
# it, records what that base's own behaviour is, and keeps a second copy of it
# as the tree the candidate's gain is grafted onto.
#
# This file is identical for every step. What differs is the per-step data files
# (FUNCS.stepN, ISA.stepN, E2E_TOL.stepN, STAGE.stepN), each shipped in the
# build context under its own step-numbered name.
FROM debian:bookworm-slim

# helpers.sh is bash (it uses arrays); /bin/sh here is dash.
SHELL ["/bin/bash", "-c"]

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash binutils ca-certificates ccache coreutils diffutils file gawk gcc \
       g++ git make nasm patch procps python3 time util-linux \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /logs/verifier /work

# Step identity and per-step data digests, written BEFORE the COPY below, so
# they name this IMAGE even when a copy is served from another step's cache
# entry. The builder identifies a build-context file by (relative path, size,
# mtime) and never reads its bytes, so two steps whose tests/ trees share a
# filename, a size and a second are indistinguishable to it. Each data file also
# carries its step number in its NAME for the same reason: distinct paths cannot
# be deduplicated against each other whatever their size and mtime. The digests
# are the second line of defence and report a substitution as a loud failure
# rather than as a plausible score.
# step-identity.sh and this Dockerfile carry no step number in their own
# names, so each carries a padding comment whose length differs per step
# instead -- the same protection, kept in one line.
#
# A RUN layer is cache-keyed on its command string, which carries this step's
# number and digests, so unlike a COPY it cannot be served from a sibling.
COPY step-identity.sh /tmp/step-identity.sh
RUN /tmp/step-identity.sh > /dev/null

# Per-step data, renamed from its step-numbered context name to the canonical
# one. The number in the source name is what defeats context deduplication.
COPY FUNCS.step05 /tests/FUNCS
COPY ISA.step05 /tests/ISA
COPY E2E_TOL.step05 /tests/E2E_TOL
COPY STAGE.step05 /tests/STAGE

# Shared, byte-identical across steps, so a substitution here is a no-op.
COPY E2E_CONFIGS /tests/E2E_CONFIGS
COPY vendor/ /tests/vendor/

# The base: the pinned tag with all six ladder stages removed, for every
# architecture. Built once here, so the candidate's tree only has to rebuild the
# kernels it touched.
RUN . /tests/vendor/PIN \
    && git clone --quiet --branch "$FFMPEG_TAG" --depth 1 "$FFMPEG_REPO" /work/ffmpeg \
    && cd /work/ffmpeg \
    && test "$(git rev-parse HEAD)" = "$FFMPEG_COMMIT" \
    && rm -rf .git \
    && python3 /tests/vendor/strip.py /work/ffmpeg in rgb ops hs out yuv2rgb \
    && cp -a /work/ffmpeg /base

# helpers.sh is copied before the build so the build below can use it.
COPY helpers.sh /tests/helpers.sh

# Build the base and record what its own suite does. Recorded rather than
# asserted: a stripped tree is entitled to differ from upstream, and the gate
# later is "no NEW failures", never "zero failures".
RUN set -eu; \
    source /tests/helpers.sh; \
    build_tree /work/ffmpeg /work/bld; \
    assert_checkasm_live /work/bld; \
    checkasm_log /work/bld /tmp/base.log 7 > /tmp/base.rc || true; \
    checkasm_failures /tmp/base.log > /base_checkasm_failures.txt; \
    wc -l < /base_checkasm_failures.txt > /etc/harbor-base-failure-count; \
    build_harness /work/ffmpeg /work/bld /h-base; \
    e2e_dump /h-base /base-dump

# test.sh is copied last on purpose: everything above is slow, and editing the
# grading script should not invalidate any of it.
COPY test.sh /tests/test.sh
RUN chmod +x /tests/test.sh
# context-uniqueness padding .....
