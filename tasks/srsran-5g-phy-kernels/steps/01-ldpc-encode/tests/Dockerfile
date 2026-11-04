# Verifier image for one step, built with that step's tests/ as the Docker
# build context. It reconstructs THIS STEP'S BASE from the pinned upstream
# commit, builds it, and keeps it as the reference: both the byte-for-byte
# correctness comparison and the speedup denominator are measured here, in the
# same container and on the same pinned cores as the candidate, so the ratio is
# self-calibrating and does not depend on how fast the grading machine is.
#
# This file is generated from one template: the only per-step difference is the
# step number. What differs between steps is data -- tests/STAGES.stepN (the
# stages this step's base lacks) and tests/FUNCS.stepN (the scoped functions).
FROM debian:bookworm-slim

# The helpers are bash, not POSIX sh: they use process substitution to walk a
# tree without spawning a subshell per file. Docker's default /bin/sh is dash,
# which fails on that with a syntax error at the point of use rather than at
# the point of the mistake.
SHELL ["/bin/bash", "-c"]

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates ccache cmake coreutils diffutils g++ gcc git \
       libfftw3-dev libgtest-dev libmbedtls-dev libsctp-dev libyaml-cpp-dev \
       make patch pkg-config procps python3 time util-linux \
    && rm -rf /var/lib/apt/lists/*

ENV CCACHE_DIR=/root/.ccache
RUN ccache --max-size=4G > /dev/null

RUN mkdir -p /app /logs/verifier /workload

# Step identity, for the binding check at the top of test.sh.
#
# Written BEFORE the COPY below, so it names this IMAGE even when the copy is
# served from another step's cache entry. The builder identifies a build-context
# file by (relative path, size, mtime) and never reads its bytes, so two steps
# whose tests/ trees share a filename, a size and a second are indistinguishable
# to it -- and the wrong milestone's verifier gets baked in at an ordinary-
# looking score. Nothing inside a Dockerfile prevents that; this only reports it.
#
# Written as a LITERAL, substituted when this file is generated, rather than as
# an ARG: harbor builds the verifier through docker-compose-build.yaml, which
# declares no `args:`, so no --build-arg can reach this build.
RUN echo "01" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh

# Bind the per-step data files to this image by digest. The digests are
# literals generated with the file, so a per-step file served from a sibling's
# build context is a loud mismatch rather than a plausible number.
RUN printf '%s\n' '09e74b0eab99463618e2010f156a812be3b6eb2ce064d8db0dc9f8021a55980f  FUNCS.step01' \
'ceba9783a9adcec53fc3a68fd74ad2eb4314f4d88a21c42b2c787bce380651d7  STAGES.step01' \
'76635626d2de4de679f49179895d51c74ef6c5d940ea165e450f794fddf5a553  OWN.step01' > /tests/DATASUMS

# This step's base tree: upstream at the pin, minus the stages this step and
# every later step must implement.
RUN . /tests/vendor/PIN \
    && git clone --quiet "$SRS_REPO" /base \
    && cd /base \
    && git checkout --quiet "$SRS_SHA" \
    && test "$(git rev-parse HEAD)" = "$SRS_SHA" \
    && rm -rf .git \
    && python3 /tests/vendor/strip.py /base $(cat /tests/STAGES.step01)

# Two source-only copies for the non-scored similarity diagnostic: the task's
# own base (the tree every step starts from, which is what "added" is measured
# against) and unstripped upstream, which is the corpus the added lines are
# looked for in. Neither is built; both stay in the verifier image, which the
# candidate cannot read.
RUN . /tests/vendor/PIN \
    && git clone --quiet "$SRS_REPO" /upstream \
    && cd /upstream \
    && git checkout --quiet "$SRS_SHA" \
    && test "$(git rev-parse HEAD)" = "$SRS_SHA" \
    && rm -rf .git \
    && cp -a /upstream /l0 \
    && python3 /tests/vendor/strip.py /l0 ldpc_encode ldpc_rate_dematch ldpc_decode precode demodulate ofh_bfp ofh_pack

RUN . /tests/vendor/PIN \
    && cmake -S /base -B /basebld $SRS_CMAKE_OPTS > /dev/null \
    && cmake --build /basebld -j4 --target $SRS_LIB_TARGETS $SRS_TEST_TARGETS > /dev/null

# The reference harness, compiled against the base tree's own libraries.
RUN . /tests/helpers.sh && build_harness /base /basebld /base_harness

# Record this base tree's OWN ctest failures. Upstream's suite has expectations
# that depend on which optimized kernels are present, so a stripped tree can
# fail tests that have nothing to do with the candidate. Correctness is judged
# relative to this list: no NEW failures.
RUN . /tests/helpers.sh \
    && run_project_tests /basebld > /base_ctest_failures.txt \
    && echo "base ctest failures: $(wc -l < /base_ctest_failures.txt)" \
    && cat /base_ctest_failures.txt

WORKDIR /app
