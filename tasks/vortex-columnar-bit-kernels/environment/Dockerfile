FROM debian:bookworm-slim

# Toolchain for a Rust workspace that builds a C/asm-adjacent dependency graph
# (prost, flatbuffers, aria) plus the shell utilities the agent needs to work.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash bc ca-certificates cmake coreutils curl diffutils g++ gcc git make \
       ninja-build patch perl pkg-config procps python3 ripgrep tar time \
       util-linux xz-utils \
    && rm -rf /var/lib/apt/lists/*

ENV RUSTUP_HOME=/usr/local/rustup \
    CARGO_HOME=/usr/local/cargo \
    PATH=/usr/local/cargo/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

# The pin ships a rust-toolchain.toml asking for 1.98.0 with rust-src, rustfmt,
# clippy and rust-analyzer; install exactly that so no `cargo` invocation in the
# tree ever has to reach the network to satisfy the manifest.
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs -o /tmp/rustup-init.sh \
    && sh /tmp/rustup-init.sh -y --no-modify-path --profile minimal --default-toolchain none \
    && rustup toolchain install 1.98.0 --profile minimal \
         -c rust-src -c rustfmt -c clippy -c rust-analyzer \
    && rm -f /tmp/rustup-init.sh \
    && rustup toolchain list

WORKDIR /app

COPY timer.sh /app/timer.sh
COPY vendor/ /tmp/vendor/
RUN chmod +x /app/timer.sh

# Base repository for step 1: upstream at the pinned commit with the seven
# bit-manipulation stages of this ladder removed. Every portable kernel stays --
# it is the correctness reference and the speedup denominator -- and so does
# everything else upstream ships: the rest of the workspace, the workspace's own
# test suite, and the divan benchmarks, minus the hand-written bench entries that
# named the removed kernels by hand.
#
# No RUSTFLAGS override and no `-C target-cpu`: selection in this project is
# runtime CPU detection plus `cfg(target_feature)`, so an override would be a way
# to reach a tier the grading host cannot execute.
RUN . /tmp/vendor/PIN \
    && git clone --quiet "$VORTEX_REPO" /app/vortex \
    && cd /app/vortex \
    && git checkout --quiet "$VORTEX_SHA" \
    && test "$(git rev-parse HEAD)" = "$VORTEX_SHA" \
    && rm -rf .git \
    && python3 /tmp/vendor/strip.py /app/vortex $STAGES

# Warm the target directory for the crates this task measures, so a candidate's
# own incremental builds are seconds rather than a cold dependency graph. This
# also populates the registry cache with exactly the crates the lockfile resolves
# for these three packages -- not the whole workspace's, which would drag in the
# CUDA and DataFusion trees.
RUN cd /app/vortex \
    && cargo build --release --locked -p vortex-buffer -p vortex-mask -p vortex-array

# strip.py enumerates every stage of the ladder, and the PIN names the exact
# commit to fetch. Neither belongs in a workspace a candidate can read.
RUN rm -rf /tmp/vendor
