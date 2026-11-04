# Agent environment. One unprivileged box, one pinned Rust toolchain, and a
# vendored SQLite build with a SHA-256 manifest -- the environment the task's
# contract is written against.
FROM rust:1.88-bookworm@sha256:af306cfa71d987911a781c37b59d7d67d934f49684058f96cf72079c3626bfe0

ENV LC_ALL=C.UTF-8 LANG=C.UTF-8 TZ=UTC \
    CARGO_TERM_COLOR=never \
    CARGO_NET_RETRY=10 \
    RUST_BACKTRACE=1

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash ca-certificates cmake curl file git jq less pkg-config procps \
        python3 ripgrep tcl unzip xxd \
    && rm -rf /var/lib/apt/lists/*

# The reference implementation of the file format this engine must interoperate
# with, built from the pinned public-domain 3.50.2 amalgamation. It is a
# development tool: it reads and writes the format, so it can be used to check
# what this engine produces. It is not the engine, and nothing in /app may
# link to, embed, or shell out to it.
COPY sqlite-src /opt/sqlite-src
RUN set -eux; \
    cd /opt/sqlite-src; \
    sha256sum -c SHA256SUMS; \
    gcc -O2 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION \
        -DSQLITE_ENABLE_DBSTAT_VTAB -DSQLITE_DEFAULT_MEMSTATUS=0 \
        -o /usr/local/bin/sqlite3 shell.c sqlite3.c -lm; \
    test "$(sqlite3 :memory: 'SELECT sqlite_version();')" = "3.50.2"; \
    rm -f /opt/sqlite-src/sqlite3.c /opt/sqlite-src/shell.c

RUN rustc --version && cargo --version && test "$(rustc --version | cut -d' ' -f2)" = "1.88.0"

# Registry cache for all fifteen steps. The agent's network reaches its model
# endpoint and nothing else, so every crate a candidate's build can name has to
# be here already: a missing one is a step that scores zero for what looks like
# a model failure. Each assets/prefetch/NN is that step's dependency set as a
# stand-in crate -- dependencies only, no implementation, no path members; see
# gen-prefetch.py -- and they share one CARGO_HOME, so what lands in the cache
# is their union. Generated from the step's own frozen Cargo.lock, so the cached
# versions, checksums and the git revision are exactly the ones the step builds.
COPY assets/prefetch /opt/prefetch
RUN set -eux; \
    for d in /opt/prefetch/*/; do \
        echo "prefetching $d"; \
        ( cd "$d" && cargo fetch ); \
    done; \
    rm -rf /opt/prefetch; \
    test -d "$CARGO_HOME/registry/cache"; \
    test -d "$CARGO_HOME/git/checkouts"

# Same widening the verifier image does, and for the same reason: some crates
# ship source with no other-read bit, and an agent that builds as a non-root
# user must not hit a permission error on a dependency it never named. The git
# checkout is widened for the same reason -- one release in the ladder pins a
# crate by git revision rather than by version.
RUN chmod -R a+rX "$CARGO_HOME/registry" "$CARGO_HOME/git" \
    && find "$CARGO_HOME/registry" "$CARGO_HOME/git" -type d -exec chmod a+w {} +

# Cargo tells the truth about this box: there is no registry to reach, and
# there will not be one for the whole of the ladder. Without this, adding a
# dependency to a manifest sends cargo to the index over a network that
# cannot answer, and the build dies on five DNS failures before it ever looks
# at the cache -- a wall a candidate hits on its own work, with an error that
# says nothing about what is actually available. With it, cargo resolves
# against the cache above: every crate this ladder compiles is there, and a
# crate that is not gives a resolution error naming the crate.
ENV CARGO_NET_OFFLINE=true

WORKDIR /app

COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh
