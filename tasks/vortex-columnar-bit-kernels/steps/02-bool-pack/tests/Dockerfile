# Verifier image for one step, built with that step's tests/ as the Docker build
# context. It reconstructs the ladder's base tree from the pinned upstream
# commit, builds it, and keeps it as the measurement denominator: the speedup is
# measured here, in the same container and on the same pinned core as the
# candidate, so the ratio is self-calibrating and does not depend on how fast the
# grading machine is.
#
# Everything down to the step-id line is byte-identical in every step's copy of
# this file, so Harbor's compose build shares those layers and no step pays for
# the clone, the dependency graph or the base suite more than once.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates coreutils curl diffutils g++ gcc git make patch procps \
       python3 tar time util-linux xz-utils \
    && rm -rf /var/lib/apt/lists/*

ENV RUSTUP_HOME=/usr/local/rustup \
    CARGO_HOME=/usr/local/cargo \
    CARGO_TARGET_DIR=/work/tgt \
    CARGO_BUILD_JOBS=4 \
    PATH=/usr/local/cargo/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

# The pin ships a rust-toolchain.toml asking for 1.98.0 with four components.
# Install exactly that, so no cargo invocation during grading reaches the
# network to satisfy the manifest.
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs -o /tmp/rustup-init.sh \
    && sh /tmp/rustup-init.sh -y --no-modify-path --profile minimal --default-toolchain none \
    && rustup toolchain install 1.98.0 --profile minimal \
         -c rust-src -c rustfmt -c clippy -c rust-analyzer \
    && rm -f /tmp/rustup-init.sh

RUN mkdir -p /app /logs/verifier /work /opt

# Byte-identical in every step, so these COPY layers are shared and a
# substitution between steps cannot change anything.
COPY vendor/PIN /tests/vendor/PIN
COPY vendor/strip.py /tests/vendor/strip.py
# The list must be present BEFORE the clone RUN below, which reads it to fill
# /opt/upstream. It is not copied by `COPY . /tests` until the end of this file,
# and a `for f in $(cat missing); do ...; done` loop silently iterates zero
# times -- `cat`'s failure is discarded inside the word list and `done` still
# exits 0, so the whole copy is skipped and the `&&` chain continues, leaving
# /opt/upstream empty.
# Byte-identical in every step, like the two above, so it shares their layer.
COPY vendor/SIMILARITY_FILES /tests/vendor/SIMILARITY_FILES
# This step's base tree -- which for this ladder is L0 for every step: the pin
# with all seven stages removed. Using one tree as every step's denominator is
# what per-function scoring needs (step k's harness times only step k's own
# functions, so a candidate who did earlier steps early gets no premium here),
# and it is why this whole block is cacheable across the five steps.
# The guard after the loop fails the build rather than ship an empty
# /opt/upstream: the similarity diagnostic reads those files and would read 0
# for every candidate without them, including the oracle it is documented to
# score ~1.0.
RUN . /tests/vendor/PIN \
    && git clone --quiet "$VORTEX_REPO" /base \
    && cd /base \
    && git checkout --quiet "$VORTEX_SHA" \
    && test "$(git rev-parse HEAD)" = "$VORTEX_SHA" \
    && mkdir -p /opt/upstream \
    && test -s /tests/vendor/SIMILARITY_FILES \
    && for f in $(cat /tests/vendor/SIMILARITY_FILES); do \
           mkdir -p "/opt/upstream/$(dirname "$f")"; cp "$f" "/opt/upstream/$f"; \
       done \
    && test "$(find /opt/upstream -type f | wc -l)" \
            -eq "$(wc -l < /tests/vendor/SIMILARITY_FILES)" \
    && rm -rf .git \
    && python3 /tests/vendor/strip.py /base $STAGES

RUN cd /base && cargo build --release -p vortex-buffer -p vortex-mask -p vortex-array

# Record this base tree's OWN suite failures. Upstream's expectations depend on
# which kernels are present, so a stripped tree fails some tests by itself.
# Correctness is judged relative to this list: no NEW failures.
RUN cd /base \
    && ( cargo test --release -p vortex-buffer -p vortex-mask -p vortex-array --lib \
         > /work/base_suite.log 2>&1 || true ) \
    && awk '/^test .* FAILED$/ { sub(/^test /, ""); sub(/ \.\.\. FAILED$/, ""); print }' \
         /work/base_suite.log | sort -u > /opt/base_failures.txt \
    && echo "base suite failures: $(wc -l < /opt/base_failures.txt)" \
    && cat /opt/base_failures.txt

# The probe harness, identical in every step, copied AFTER the expensive shared
# work so that editing it does not invalidate the base build above.
COPY vendor/harness /tests/vendor/harness

# The base's own probe binary, and the target directory it and the candidate
# share. External crates are keyed on name+version+features rather than on the
# tree they sit under, so the candidate's build at grading time reuses every
# third-party artifact here and pays only for the vortex crates and the probe.
RUN mkdir -p /work/baseprobe/src \
    && cp /tests/vendor/harness/probe.rs /work/baseprobe/src/main.rs \
    && sed -e 's#@TREEROOT@#/base#g' /tests/vendor/harness/Cargo.toml.in \
         > /work/baseprobe/Cargo.toml \
    && cd /work/baseprobe && cargo build --release \
    && cp /work/tgt/release/probe /opt/baseprobe \
    && chmod 755 /opt/baseprobe \
    && cp /work/baseprobe/Cargo.lock /opt/probe.Cargo.lock \
    && rm -rf /work/baseprobe

# Step identity, for the `require_step` check at the top of test.sh.
#
# Written BEFORE `COPY . /tests` so it names this IMAGE even when that copy is
# served from another step's cache entry. The builder identifies a build-context
# file by (relative path, size, mtime) and never reads its bytes, so two steps
# whose tests/ trees share a filename, a size and a second are indistinguishable
# to it -- and the wrong milestone's verifier gets baked in at an
# ordinary-looking score. Nothing inside a Dockerfile prevents that; this only
# reports it.
#
# A per-step literal rather than an ARG: harbor builds the verifier through
# docker-compose-build.yaml, which declares no `args:`, so no --build-arg can
# reach this build.
RUN echo "02" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh

WORKDIR /app

# Build-context disambiguation. The builder keys a context file on (relative
# path, size, mtime) and never reads its bytes, so two steps whose Dockerfiles
# share that key are built with each other's tests -- the
# wrong milestone's verifier, at an ordinary-looking score. The step id above is
# fixed width and every copy of this file is otherwise identical, so this
# trailing tag makes each step's copy a different SIZE as well as a different
# mtime: a copy of the package that re-stamps timestamps (cp -r, a checkout)
# cannot put the five back on one key.
# step-tag: ##
