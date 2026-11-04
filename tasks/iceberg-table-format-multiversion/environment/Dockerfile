FROM rust:1.88-bookworm

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash build-essential ca-certificates git jq pkg-config procps python3 ripgrep \
    && rm -rf /var/lib/apt/lists/*

ENV CARGO_TERM_COLOR=never

# Prefetch every step's pinned dependency graph into CARGO_HOME (the same
# candidate + driver manifests the verifier images warm from). The agent phase
# is allowlisted and cannot reach crates.io; this cache is the whole resolvable
# crate graph across the step ladder.
COPY assets/prefetch /opt/prefetch
RUN set -eu; \
    for d in /opt/prefetch/*/*/; do \
        echo "prefetching $d"; \
        ( cd "$d" && ( cargo fetch --locked || cargo fetch ) ); \
    done \
    && rm -rf /opt/prefetch
ENV CARGO_NET_OFFLINE=true
RUN chmod -R a+rX "$CARGO_HOME/registry" \
    && find "$CARGO_HOME/registry" -type d -exec chmod a+w {} +
WORKDIR /app

# Agent-visible, informational countdown for the per-step timeout configured in
# task.toml. Harbor remains responsible for enforcing the hard limit.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh
