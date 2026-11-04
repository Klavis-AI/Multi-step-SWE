# Agent environment for tls13-protocol-stack-multiversion.
#
# The Rust version and RUSTFLAGS below match each step's tests/Dockerfile and
# tests/test.sh, so the container the candidate builds in and the container
# that grades it do not drift apart. Change them together.
FROM rust:1.98.1-bookworm

# The rust image already carries gcc, make, perl, git and curl. cmake is here
# because aws-lc-rs builds C from step 10 onwards; ripgrep, procps and python3
# are ordinary working tools an agent expects to find.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates cmake pkg-config procps python3 ripgrep \
    && rm -rf /var/lib/apt/lists/*

# The Go toolchain, for the same reason the verifier image carries it: step
# 10's `bogo` crate asks rustls for the `fips` feature unconditionally and
# aws-lc-fips-sys will not build without Go. A candidate whose workspace cannot
# build in its own container has been handed an impossible step, so this has to
# match the grader rather than be left to the candidate to discover.
COPY --from=golang:1.26-bookworm /usr/local/go /usr/local/go
ENV PATH=/usr/local/go/bin:$PATH

ENV CARGO_TERM_COLOR=never LC_ALL=C LANG=C

# Identical to the flags the verifier builds with. The releases on this ladder
# deny lint sets written for compilers much older than this one, and several of
# those lints now fire on untouched upstream source; without the cap the first
# build in a fresh workspace fails on code the candidate did not write. Nothing
# is graded on lints.
ENV RUSTFLAGS="--cap-lints=allow"

# Registry cache for all thirteen steps. One workspace is carried up the whole
# ladder, so the crates needed at step 13 are known at step 1; downloading them
# once at image build time means no step spends its budget on a mirror. Each
# assets/prefetch/NN is a dependency-graph stand-in with no implementation in
# it -- see environment/assets/gen-prefetch.py, whose agent-side rule keeps the
# published `rustls` out of the cache wherever the build lines allow -- and
# they share one CARGO_HOME, so what lands in the cache is their union.
COPY assets/prefetch /opt/prefetch
RUN set -eu;     for d in /opt/prefetch/*/; do         echo "prefetching $d"; ( cd "$d" && cargo fetch );     done;     rm -rf /opt/prefetch

# Same widening the verifier image does, and for the same reason: some crates
# ship source with no other-read bit, and an agent that chooses to build as a
# non-root user must not hit a permission error on a dependency it never named.
RUN chmod -R a+rX "$CARGO_HOME/registry"     && find "$CARGO_HOME/registry" -type d -exec chmod a+w {} +

# Keep this script, environment/timer.sh in every step's workdir/, and every
# steps.agent.timeout_sec in task.toml synchronized. Harbor enforces the real
# timeout; this is the agent-visible countdown.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh

WORKDIR /app
