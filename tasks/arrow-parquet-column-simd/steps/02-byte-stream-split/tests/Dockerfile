# Verifier image for one step.  Built with that step's tests/ as the Docker
# build context.  It reconstructs the ladder's base (level L0) from the pinned
# upstream commit, builds it, and keeps it as the no-op reference: the speedup
# denominator is measured here, in the same container and on the same pinned
# core as the candidate, so the ratio is self-calibrating and does not depend on
# how fast the target machine is.
#
# THIS FILE IS IDENTICAL FOR EVERY STEP, and so is every instruction above the
# per-step COPY near the bottom.  That is deliberate: BuildKit reuses a layer
# only when the instruction and everything it reads are unchanged, so keeping
# the clone/strip/build work upstream of the first per-step file lets all six
# verifier images share one ~50-minute base build.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates ccache cmake coreutils diffutils g++ gcc git \
       libthrift-dev make patch procps python3 python-is-python3 rsync time \
       util-linux zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

ENV CCACHE_DIR=/ccache
ENV CCACHE_MAXSIZE=30G
ENV BUILD_JOBS=4

RUN mkdir -p /app /logs/verifier /tests/vendor /td /probe /baseline

# ---- shared, byte-identical in every step --------------------------------
COPY vendor/PIN             /tests/vendor/PIN
COPY vendor/base_stages.txt /tests/vendor/base_stages.txt
COPY vendor/test_targets.txt /tests/vendor/test_targets.txt
COPY vendor/strip.py        /tests/vendor/strip.py
COPY vendor/build_arrow.sh  /tests/vendor/build_arrow.sh
COPY vendor/build_probe.sh  /tests/vendor/build_probe.sh
COPY vendor/build_base_libs.sh   /tests/vendor/build_base_libs.sh

# Clones the pin, strips every stage the ladder removes, builds the base, fetches the
# upstream test data, and records the base's own test failures.  This is the
# ~50-minute layer; nothing below this point may be read by it.
RUN bash /tests/vendor/build_base_libs.sh

COPY vendor/kbench.cc            /tests/vendor/kbench.cc
COPY vendor/e2e.cc               /tests/vendor/e2e.cc
COPY vendor/build_base_probes.sh /tests/vendor/build_base_probes.sh

# The two measurement probes, the upstream test data, and the base tree's own
# test-failure record.  Kept in their own layer so that editing any of them
# costs seconds rather than the Arrow build above.
RUN bash /tests/vendor/build_base_probes.sh

# ---- per-step data: cheap layers -----------------------------------------
COPY . /tests

# Step identity, for the binding check at the top of test.sh.
#
# The step number is DERIVED from the uniquely named per-step data file rather
# than written as a literal, so that six byte-identical test trees cannot be
# told apart by a one-character difference.  FUNCS.step<N> carries the number in
# its NAME, so two steps can never collide in the build context.
#
# The step-id file names this IMAGE.  test.sh compares it with the number it reads
# out of the /tests tree it was handed, and with a digest taken here, so a
# substitution reports step_mismatch instead of eight plausible numbers.
RUN set -eu; \
    cd /tests; \
    step=$(ls FUNCS.step* | sed 's/^FUNCS\.step//'); \
    [ -n "$step" ]; \
    [ "$(printf '%s\n' $step | wc -l)" = 1 ]; \
    [ -f "ISA.step$step" ]; \
    [ -f "SCOPE_FILES.step$step" ]; \
    printf '%s' "$step" > /etc/harbor-step-id; \
    sha256sum FUNCS.step* ISA.step* SCOPE_FILES.step* test.sh helpers.sh check_isa.sh \
        > /etc/harbor-scope.sha256; \
    chmod +x /tests/test.sh

WORKDIR /app
