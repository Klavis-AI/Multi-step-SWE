# Verifier image for step 01-sw-score.
#
# Built with this step's tests/ as the build context.  It reconstructs THIS
# STEP'S BASE from the pinned upstream commit -- the families this step and
# every later step must implement are removed -- and keeps it as the reference:
# the speedup denominator, the correctness oracle and the fixed header the
# harness is compiled against all come from here, so the ratio is
# self-calibrating and the candidate cannot influence the denominator.
#
# This file is otherwise identical for every step.  What differs is
# tests/FUNCS.step1, tests/BASE_STAGES.step1, tests/WORKLOAD.step1 and
# tests/GUARD.step1, each of which carries the step number in its NAME so the
# builder's (path, size, mtime) context dedup cannot serve a sibling's copy.
# The trailing comment keeps this file's own size unique per step.
#.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash binutils ca-certificates coreutils diffutils gcc g++ git make \
       meson ninja-build patch pkg-config procps python3 util-linux zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /logs/verifier

# Step identity, written before the COPY below so it names this IMAGE even if
# the copy is served from another step's cache entry.
RUN echo "1" > /etc/harbor-step-id

# Digests of this step's own build-context files, computed from the package
# and baked here, so they come from outside the context.  test.sh
# re-checks them before grading anything: a context served from a sibling's
# cache entry cannot match, and the mismatch is reported as a step_mismatch
# rather than graded as a plausible number.
RUN cat > /etc/harbor-step-digests <<'DIGESTS'
2289cf6452d3666eda62e44ecaabfacf12da9de5803f9ba8a24656b6ea0e9da9  BASE_STAGES.step1
4d51a97c278b7567c5fbfd1bbbf9a430966d72db550c07142a687fa47f4f04e3  FUNCS.step1
642823f38b394f4a9c73403b7fe8b58cdd0b77d1c55ace90ae3308357eab0b37  FUTURE.step1
31029ea84894564755ee2020fb488abd1bc28695a1dc83f61f538b19c2c27e6c  GUARD.step1
fd2f10876c01def13865c1ed130d4350fc6dd5218f9b3e3ca267486e3349c2c2  ISA.step1
670e8581a744d8013152cf0a80dc8cfcb3afe3368bae2bd32cc6c8181b4afdb5  STAGE.step1
a142e8100b06c7d49dd0f4b33867f0a351747d127edec568e18f684cfb71ce71  WORKLOAD.step1
0d2ddd316e9c6eb4c81e297094acdaa66e6f6650034e778d469cda6831741d6c  helpers.sh
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
    && python3 /tests/vendor/strip.py /base $(cat /tests/BASE_STAGES.step1)

# Build the base once, here rather than at grading time: it is built from the
# pinned upstream sources, so the candidate cannot influence it, and every run
# only has to build the candidate.
RUN meson setup /basebld /base --buildtype=release > /dev/null     && ninja -C /basebld -j"$(nproc)" > /dev/null

WORKDIR /app
