# Verifier image for one step, built with that step's tests/ as the Docker
# build context.
#
# The base tree is the ladder's L0 -- upstream at the pin with every ladder
# stage's per-ISA implementation removed and its dispatch entries rewired to the
# portable C reference.  One tree serves three purposes in the grading run: the
# denominator for every per-function speedup, the floor for the end-to-end
# non-regression guard, and the baseline for the project's own suite.  It is
# L0 rather than "this step's base" on purpose: a no-op candidate's tree IS L0
# at every step, so a guard baselined on L(k-1) would call a no-op catastrophic.
#
# This whole block depends only on vendor/ and the pin, both byte-identical
# across steps, so the expensive layers are shared by all eight images.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates cmake coreutils diffutils g++ gcc gawk git make nasm \
       ninja-build \
       patch procps python3 time util-linux \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /logs/verifier /workload

COPY vendor/ /tests/vendor/
RUN chmod +x /tests/vendor/build_bench.sh

RUN . /tests/vendor/PIN \
    && git clone --quiet "$SVTPIN_REPO" /tmp/src \
    && cd /tmp/src \
    && git checkout --quiet "$SVTPIN_SHA" \
    && test "$(git rev-parse HEAD)" = "$SVTPIN_SHA" \
    && rm -rf .git \
    && python3 /tests/vendor/prepare.py /tmp/src /base \
    && python3 /tests/vendor/strip.py /base \
         nlt_enc dwt_fwd quant gcli rate_ctrl unpack idwt nlt_dec

RUN cmake -S /base -B /basebld -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF -DBUILD_TESTING=ON \
        -DCMAKE_OUTPUT_DIRECTORY=/baseout > /basebld.cmake.log 2>&1 \
    && ninja -C /basebld >> /basebld.cmake.log 2>&1

# The base build's own speed measurement, in the image, so the grading run only
# has to run it: the denominator is measured in the same container and on the
# same pinned cores as the candidate.
RUN bash /tests/vendor/build_bench.sh /base /basebld /tests/vendor/bench /h-base

# The project's own suite does not all pass in a stripped tree, and a stripped
# tree is what the candidate starts from.  Record exactly which cases fail here,
# and in the grading run count only NEW failures.
RUN LD_LIBRARY_PATH=/baseout /baseout/SvtJpegxsUnitTests > /base_ut.log 2>&1 || true; \
    grep -oE '^\[  FAILED  \] [^ ]+' /base_ut.log | awk '{print $4}' | sort -u > /base_ut_failures.txt; \
    echo "base unit-test failures: $(wc -l < /base_ut_failures.txt)"; \
    grep -c '^\[       OK \]' /base_ut.log | sed 's/^/base unit-test passes: /'

# Workload: regenerated here, never read from /app.
RUN python3 /tests/vendor/make_workload.py /workload/guard.yuv 640 480 24 yuv422 8 \
    && python3 /tests/vendor/make_workload.py /workload/in_yuv422_8.yuv 320 240 4 yuv422 8 \
    && python3 /tests/vendor/make_workload.py /workload/in_yuv420_8.yuv 320 240 4 yuv420 8 \
    && python3 /tests/vendor/make_workload.py /workload/in_yuv444_8.yuv 320 240 4 yuv444 8 \
    && python3 /tests/vendor/make_workload.py /workload/in_yuv422_10.yuv 320 240 4 yuv422 10 \
    && python3 /tests/vendor/make_workload.py /workload/in_yuv420_10.yuv 320 240 4 yuv420 10 \
    && python3 /tests/vendor/make_workload.py /workload/in_yuv444_10.yuv 320 240 4 yuv444 10

# ---------------------------------------------------------------------------
# Step identity.  Written as a LITERAL, placed before the per-step COPYs so it
# names THIS image even when a copy below was served from another step's cache
# entry.
RUN echo "08" > /etc/harbor-step-id
# Context-uniqueness padding for this step. --------------------------------

COPY FUNCS.08 /tests/FUNCS
COPY STAGE.08 /tests/STAGE
COPY ISA.08 /tests/ISA
# Digest of the per-step data files, bound into the image by a RUN (cache-keyed
# on the command text, so it cannot be served from a sibling) and verified at
# the top of test.sh.
RUN cd /tests && printf '%s  %s\n' "$(sha256sum FUNCS | cut -d' ' -f1)" FUNCS \
                              "$(sha256sum STAGE | cut -d' ' -f1)" STAGE \
                              "$(sha256sum ISA   | cut -d' ' -f1)" ISA \
        > /etc/harbor-step-datasums

COPY helpers.sh /tests/helpers.sh
COPY check_isa.py /tests/check_isa.py
COPY check_cref.py /tests/check_cref.py
COPY test.sh /tests/test.sh
RUN chmod +x /tests/test.sh

WORKDIR /app
