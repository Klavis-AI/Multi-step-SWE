# Verifier image for one milestone, built with that milestone's tests/ as the
# Docker build context.  It reconstructs THIS STEP'S BASE from the pinned
# upstream commit, builds it, runs its own suite, and keeps both the source and
# the build as the no-op reference: the speedup denominator is measured here, in
# the same container and on the same pinned cores as the candidate, so the ratio
# is self-calibrating and does not depend on how fast the grading machine is.
#
# This file differs from step to step only in the step-id literal below, which
# is written BEFORE the COPY so that it names this IMAGE even when the copy is
# served from another step's cache entry.  The builder identifies a build-context
# file by (relative path, size, mtime) and never reads its bytes, so two steps
# whose tests/ trees share a filename, a size and a second are indistinguishable
# to it -- and the wrong milestone's verifier gets baked in at an ordinary-looking
# score.  Nothing inside a Dockerfile prevents that; this only reports it, and
# test.sh refuses to grade unless the data files hash to what this image was
# built with.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates cmake coreutils diffutils findutils g++ gcc gawk git \
       make patch procps python3 time util-linux \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /logs/verifier /workload

RUN echo "01" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh /tests/check_isa.sh

# This milestone's base tree: upstream at the pin with every family of the
# ladder routed at the portable reference.  It is the same tree for every step
# -- what a step asks for is what its patch restores, and what the base already
# has is the scalar reference plus anything the candidate built earlier.
# The pin is kept twice: /upstream is the unmodified tree, used only by the
# non-scored similarity diagnostic, and /base is its stripped copy, which is
# this milestone's reference and the speedup denominator.
RUN . /tests/vendor/PIN \
    && git clone --quiet "$SIMDUTF_REPO" /upstream \
    && cd /upstream \
    && git checkout --quiet "$SIMDUTF_SHA" \
    && test "$(git rev-parse HEAD)" = "$SIMDUTF_SHA" \
    && rm -rf .git \
    && cp -a /upstream /base \
    && python3 /tests/vendor/strip.py /base \
         utf8_to_utf16_and_utf32 utf16_to_utf8 utf32_to_utf8 utf16_utf32 \
         latin1 base64 validate_misc

RUN cmake -S /base -B /basebld -DCMAKE_BUILD_TYPE=Release -DSIMDUTF_TESTS=ON \
      -DSIMDUTF_BENCHMARKS=OFF -DSIMDUTF_ALWAYS_INCLUDE_FALLBACK=ON > /dev/null \
    && cmake --build /basebld -j4 > /dev/null

# Record this base tree's OWN ctest failures.  The stripped tree is a supported
# configuration and passes everything today, but if it ever did not, correctness
# would be judged relative to this list: no NEW failures.
RUN ctest --test-dir /basebld -j4 > /base_ctest.log 2>&1 || true; \
    sed -n '/tests FAILED/,$p' /base_ctest.log \
      | grep -oE '^[[:space:]]+[0-9]+ - [^ ]+' | awk '{print $3}' | sort -u \
      > /base_ctest_failures.txt; \
    echo "base ctest failures: $(wc -l < /base_ctest_failures.txt)"; \
    cat /base_ctest_failures.txt

# Bind this image to its data files.  test.sh refuses to grade unless the files
# under /tests hash to this, so a context served from a sibling step is a loud
# step_mismatch rather than eight plausible numbers.
RUN cd /tests && sha256sum FUNCS.step01 DRAWS.step01 RATIO_CAP.step01 \
      > /etc/harbor-step-binding

WORKDIR /app
