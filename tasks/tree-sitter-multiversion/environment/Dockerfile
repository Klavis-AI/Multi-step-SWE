# Agent environment. Carries the toolchains a parser generator and its C
# runtime need, and nothing about the milestones: the task material lives in
# each step's workdir, not in this image.
FROM node:22-bookworm-slim AS nodesrc

FROM rust:1.98-bookworm

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash ca-certificates coreutils curl diffutils gawk git \
        procps python3 ripgrep util-linux \
        clang libclang-dev pkg-config build-essential \
    && rm -rf /var/lib/apt/lists/*

COPY --from=nodesrc /usr/local/bin/node /usr/local/bin/node
COPY --from=nodesrc /usr/local/bin/npm /usr/local/bin/npm
COPY --from=nodesrc /usr/local/lib/node_modules /usr/local/lib/node_modules

# The agent's own build resolves Rust crates and the agent phase has no
# network, so the registry cache is warmed here, at image build time, from the
# same union of release anchors' locked dependency graphs the verifier images
# bake (assets/prewarm/, manifests and empty target stubs only, ~2 MB; the
# crates themselves land in $CARGO_HOME). Nothing in prewarm/ is compiled or
# linked, and it is removed once the fetch is done. Cargo is then configured
# offline so `cargo build` resolves from the cache instead of dying on a DNS
# lookup; a fetch that does not resolve fails this build, because unlike the
# verifier image there is no network to fall back to.
COPY assets/prewarm /opt/prewarm
RUN set -eu; \
    for d in /opt/prewarm/*/; do ( cd "$d" && cargo fetch --locked ); done; \
    rm -rf /opt/prewarm; \
    chmod -R a+rX "$CARGO_HOME/registry"; \
    find "$CARGO_HOME/registry" -type d -exec chmod a+w {} +; \
    cfg="$CARGO_HOME/config.toml"; \
    [ -f "$cfg" ] || printf '' > "$cfg"; \
    grep -q '^\[net\]' "$cfg" || printf '\n[net]\noffline = true\n' >> "$cfg"

WORKDIR /app

# Agent-visible countdown. Harbor enforces the real timeout; this only lets the
# agent pace itself. Keep it synchronized with every step's agent timeout_sec.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh
