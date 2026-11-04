# Verifier image for step 06-nw-detail.
#
# Built with this step's tests/ as the build context.  It reconstructs THIS
# STEP'S BASE from the pinned upstream commit -- the families this step and
# every later step must implement are removed -- and keeps it as the reference:
# the speedup denominator, the correctness oracle and the fixed header the
# harness is compiled against all come from here, so the ratio is
# self-calibrating and the candidate cannot influence the denominator.
#
# This file is otherwise identical for every step.  What differs is
# tests/FUNCS.step6, tests/BASE_STAGES.step6, tests/WORKLOAD.step6 and
# tests/GUARD.step6, each of which carries the step number in its NAME so the
# builder's (path, size, mtime) context dedup cannot serve a sibling's copy.
# The trailing comment keeps this file's own size unique per step.
#......
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash binutils ca-certificates coreutils diffutils gcc g++ git make \
       meson ninja-build patch pkg-config procps python3 util-linux zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /logs/verifier

# Step identity, written before the COPY below so it names this IMAGE even if
# the copy is served from another step's cache entry.
RUN echo "6" > /etc/harbor-step-id

# Digests of this step's own build-context files, computed from the package
# and baked here, so they come from outside the context.  test.sh
# re-checks them before grading anything: a context served from a sibling's
# cache entry cannot match, and the mismatch is reported as a step_mismatch
# rather than graded as a plausible number.
RUN cat > /etc/harbor-step-digests <<'DIGESTS'
2715b36fb8852f2cbb5d72661f6e821d2bc53ec6fd9afdf9621b38feced4eef1  BASE_STAGES.step6
cc8af663a8d41842954426ed0d0a2720921d28fcc6435bd64a31240163c11eae  FUNCS.step6
91faf19458cc9fd4224aa5b8ed81fff78b0befd6b4db0c40768dc6077283b16b  FUTURE.step6
31029ea84894564755ee2020fb488abd1bc28695a1dc83f61f538b19c2c27e6c  GUARD.step6
fd2f10876c01def13865c1ed130d4350fc6dd5218f9b3e3ca267486e3349c2c2  ISA.step6
d2a063cb92f1359ca2f01d06f03402cdb8e3aa2cbab8b468745edb4a65b8f30c  STAGE.step6
361dd911f46902afda534c6ee430149042f1fdd9eb61983ea66529dc3e4634b8  WORKLOAD.step6
925ba04cf09c1a5f1e45a01b72caa0786ddbbc938895cbf1d88341b71065bf3c  helpers.sh
ec2dcc0144fd8243e33d82ce465780b62e599f318ec25038915eef7dac94db38  test.sh
37817d902c35ae4a4a114531cb698450e130299c6f20e08430840144a8cd461b  vendor/PIN
d671833fb32f11df6a94145a16682ae55a92fe6a8d6ebf0c88c82dddf4f69076  vendor/bench_stage.c
40e633adaf04c7a99b4f8e587a9701a60805987010ac1b08f5caea5416d74bc1  vendor/envelope.py
81bcfdd03d1bff49e2a08a1294a5c76ae785073b8968ab5d65c7f5940ed14161  vendor/strip.py

DIGESTS

COPY . /tests
RUN chmod +x /tests/test.sh

# This step's base tree: upstream at the pin, minus the families this step and
# every later step must implement.
RUN . /tests/vendor/PIN \
    && git clone --quiet "$PARASAIL_REPO" /base \
    && cd /base \
    && git checkout --quiet "$PARASAIL_SHA" \
    && test "$(git rev-parse HEAD)" = "$PARASAIL_SHA" \
    && rm -rf .git \
    && python3 /tests/vendor/strip.py /base $(cat /tests/BASE_STAGES.step6)

# Build the base once, here rather than at grading time: it is built from the
# pinned upstream sources, so the candidate cannot influence it, and every run
# only has to build the candidate.
RUN meson setup /basebld /base --buildtype=release > /dev/null     && ninja -C /basebld -j"$(nproc)" > /dev/null

WORKDIR /app
