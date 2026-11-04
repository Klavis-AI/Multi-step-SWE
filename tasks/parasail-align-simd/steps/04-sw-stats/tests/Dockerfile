# Verifier image for step 04-sw-stats.
#
# Built with this step's tests/ as the build context.  It reconstructs THIS
# STEP'S BASE from the pinned upstream commit -- the families this step and
# every later step must implement are removed -- and keeps it as the reference:
# the speedup denominator, the correctness oracle and the fixed header the
# harness is compiled against all come from here, so the ratio is
# self-calibrating and the candidate cannot influence the denominator.
#
# This file is otherwise identical for every step.  What differs is
# tests/FUNCS.step4, tests/BASE_STAGES.step4, tests/WORKLOAD.step4 and
# tests/GUARD.step4, each of which carries the step number in its NAME so the
# builder's (path, size, mtime) context dedup cannot serve a sibling's copy.
# The trailing comment keeps this file's own size unique per step.
#....
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash binutils ca-certificates coreutils diffutils gcc g++ git make \
       meson ninja-build patch pkg-config procps python3 util-linux zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /logs/verifier

# Step identity, written before the COPY below so it names this IMAGE even if
# the copy is served from another step's cache entry.
RUN echo "4" > /etc/harbor-step-id

# Digests of this step's own build-context files, computed from the package
# and baked here, so they come from outside the context.  test.sh
# re-checks them before grading anything: a context served from a sibling's
# cache entry cannot match, and the mismatch is reported as a step_mismatch
# rather than graded as a plausible number.
RUN cat > /etc/harbor-step-digests <<'DIGESTS'
bf0180bd2c24d71455873b474877fc630357f6a187ac68ada54610256df33cf7  BASE_STAGES.step4
7cf985fc80ddf22234f54606a2a63079fd734c5fdb3e10a496611d7c4dcb8c68  FUNCS.step4
cb94ac4c81ed018d984b6fe7735cd3223af29a39552fd065b8a8593cb640fcea  FUTURE.step4
31029ea84894564755ee2020fb488abd1bc28695a1dc83f61f538b19c2c27e6c  GUARD.step4
fd2f10876c01def13865c1ed130d4350fc6dd5218f9b3e3ca267486e3349c2c2  ISA.step4
1a9f66bfdc621859e07dc21c80c257193ebfa6e907c337f25c71767aa954d0f1  STAGE.step4
9c46f505420fb4d471f116f134219cc00a4d23e642857e3c0d41f448e8409689  WORKLOAD.step4
8ce037f0bdc70fe8df439fc7a5fe7237b645377e5c0fb795bc4122ea26d095e1  helpers.sh
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
    && python3 /tests/vendor/strip.py /base $(cat /tests/BASE_STAGES.step4)

# Build the base once, here rather than at grading time: it is built from the
# pinned upstream sources, so the candidate cannot influence it, and every run
# only has to build the candidate.
RUN meson setup /basebld /base --buildtype=release > /dev/null     && ninja -C /basebld -j"$(nproc)" > /dev/null

WORKDIR /app
