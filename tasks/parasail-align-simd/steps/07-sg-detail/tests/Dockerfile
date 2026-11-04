# Verifier image for step 07-sg-detail.
#
# Built with this step's tests/ as the build context.  It reconstructs THIS
# STEP'S BASE from the pinned upstream commit -- the families this step and
# every later step must implement are removed -- and keeps it as the reference:
# the speedup denominator, the correctness oracle and the fixed header the
# harness is compiled against all come from here, so the ratio is
# self-calibrating and the candidate cannot influence the denominator.
#
# This file is otherwise identical for every step.  What differs is
# tests/FUNCS.step7, tests/BASE_STAGES.step7, tests/WORKLOAD.step7 and
# tests/GUARD.step7, each of which carries the step number in its NAME so the
# builder's (path, size, mtime) context dedup cannot serve a sibling's copy.
# The trailing comment keeps this file's own size unique per step.
#.......
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash binutils ca-certificates coreutils diffutils gcc g++ git make \
       meson ninja-build patch pkg-config procps python3 util-linux zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /logs/verifier

# Step identity, written before the COPY below so it names this IMAGE even if
# the copy is served from another step's cache entry.
RUN echo "7" > /etc/harbor-step-id

# Digests of this step's own build-context files, computed from the package
# and baked here, so they come from outside the context.  test.sh
# re-checks them before grading anything: a context served from a sibling's
# cache entry cannot match, and the mismatch is reported as a step_mismatch
# rather than graded as a plausible number.
RUN cat > /etc/harbor-step-digests <<'DIGESTS'
f3f53f3953b7b197465644d1f8b4fae1cf6757ee431f0b122abe957933b08a8c  BASE_STAGES.step7
ecb7b4845a18e56f9ae170314aecb9d760fb758dc6ee4490c098711af5c1f37e  FUNCS.step7
01ba4719c80b6fe911b091a7c05124b64eeece964e09c058ef8f9805daca546b  FUTURE.step7
31029ea84894564755ee2020fb488abd1bc28695a1dc83f61f538b19c2c27e6c  GUARD.step7
fd2f10876c01def13865c1ed130d4350fc6dd5218f9b3e3ca267486e3349c2c2  ISA.step7
f3f53f3953b7b197465644d1f8b4fae1cf6757ee431f0b122abe957933b08a8c  STAGE.step7
5301370e592384baf6f08839501cf76f485d51ff89b13db5527b6a5b522e1d15  WORKLOAD.step7
9b58c48e6d505ad2f7c6ec8faf944734d7b31325448c52db7f7cd997dc0bcb40  helpers.sh
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
    && python3 /tests/vendor/strip.py /base $(cat /tests/BASE_STAGES.step7)

# Build the base once, here rather than at grading time: it is built from the
# pinned upstream sources, so the candidate cannot influence it, and every run
# only has to build the candidate.
RUN meson setup /basebld /base --buildtype=release > /dev/null     && ninja -C /basebld -j"$(nproc)" > /dev/null

WORKDIR /app
