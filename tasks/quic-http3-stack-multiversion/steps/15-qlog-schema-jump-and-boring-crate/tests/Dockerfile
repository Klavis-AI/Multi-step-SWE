# Verifier image for step 15 (quiche 0.30.0). Built with
# steps/15-qlog-schema-jump-and-boring-crate/tests/ as the Docker build context: it cannot reach the task
# root, environment/, or another step, which is why helpers.sh, the asset corpus
# and the prewarm manifests are duplicated per step.
#
# The candidate's source arrives later through the declared /app artifact; it is
# never part of this image.
FROM rust:1.88-bookworm

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash build-essential ca-certificates clang libclang-dev cmake coreutils diffutils \
       gawk git golang-go iproute2 jq libev-dev ninja-build perl pkg-config \
       procps python3 ripgrep tar uuid-dev xxd \
    && rm -rf /var/lib/apt/lists/*

ENV CARGO_TERM_COLOR=never
ENV CARGO_HOME=/usr/local/cargo
ENV CARGO_TARGET_DIR=/opt/cargo-target

# Step identity, for the binding check at the top of test.sh.
#
# Written BEFORE any COPY of the build context, so it names this IMAGE even when
# a copy is served from another step's cache entry. The builder identifies a
# local context file by (relative path, size, mtime-to-the-second) and never
# reads its bytes, so two steps whose tests/ trees share a filename, a size and
# a second are indistinguishable to it -- and the wrong milestone's verifier
# gets baked in at an ordinary-looking score. Nothing inside a Dockerfile
# prevents that; this only reports it.
#
# Written as a LITERAL, substituted when this file is generated, rather than as
# an ARG: harbor builds the verifier through docker-compose-build.yaml, which
# declares no `args:`, so no --build-arg can reach this build and an ARG default
# would be an override surface nothing can use.
#
# Keep this number, the step directory's number and test.sh's require_step call
# equal. bin/check-binding.sh checks all three.
RUN echo "15" > /etc/harbor-step-id

# --- BoringSSL ---------------------------------------------------------------
# This release deleted the BoringSSL submodule, so there is no sha to pin and
# nothing to prebuild here. The transport crate now takes its BoringSSL through
# the `boring` crate, and `boring-sys` compiles its own copy as ordinary
# dependency work in the prewarm layer below.
#
# bssl-build.sh sits at the top of tests/ rather than inside assets/ on purpose:
# it is build-time machinery, not part of the corpus the driver digests, and
# keeping it out means a toolchain change does not invalidate every step's
# fixture pin. It is byte-identical in all fifteen steps, so steps sharing a pin
# share this layer.
COPY bssl-build.sh /usr/local/bin/bssl-build.sh
# Copied for layer-sharing with the other fourteen steps, but never invoked at
# this one. There is deliberately no `ENV QUICHE_BSSL_PATH` line: the variable
# is unset, not set to an empty string, so a candidate whose build script still
# reads it gets no accidental help.

# --- dependency prewarm ------------------------------------------------------
# Two stub crates carrying this release's exact dependency graph and the
# harness's own. `cargo fetch --locked` populates the registry cache and
# `cargo build` compiles the graph into the shared target directory, so grading
# pays for the candidate's code and nothing else. Stub sources, not the
# milestone's source: the image needs the dependencies, not the answer.
COPY assets/prewarm /opt/prewarm
RUN cd /opt/prewarm/candidate && cargo fetch --locked && cargo build --locked -j 4 \
    && cd /opt/prewarm/harness && cargo fetch --locked && cargo build --locked -j 4

# The prewarm above compiles the release's own stub crates -- the root package
# at steps 01-05, the workspace members from 06 on -- and a stub carries the
# milestone's own name, version and dependency list. Its artefacts in the
# shared target directory are therefore indistinguishable from the candidate's
# own crate: cargo's fingerprint is keyed by the package's name, version,
# features, dependencies and relative source layout, not by where the tree
# sits. A candidate crate with the release's name and version is then reported
# `Fresh`, its dependents are linked against the empty stub, and the whole
# workspace fails to build -- vetoing the step on the candidate's proof of
# life. The reference tree *is* the release, so it always meets this; a
# candidate whose versions or dependencies differ does not, which is why this
# only ever showed up as the oracle scoring zero.
#
# Nothing is lost by removing them: what the prewarm is for is the third-party
# graph, and these are three-line stubs. `cargo clean -p` takes out exactly the
# named package's artefacts and fingerprints, so the candidate's own crates are
# compiled from the candidate's own source, always.
RUN set -eu; cd /opt/prewarm/candidate; \
    for d in . */; do \
        [ -f "$d/Cargo.toml" ] || continue; \
        n=$(sed -n 's/^name[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' \
            "$d/Cargo.toml" | head -1); \
        [ -n "$n" ] || continue; \
        echo "pruning prewarm stub: $n"; \
        cargo clean -p "$n" || echo "  ($n is not a workspace member)"; \
    done

# A second skeleton, carrying the PREVIOUS release's dependency graph. Fetched
# only -- deliberately no `cargo build`, so nothing reaches the shared target
# directory and the milestone prewarm above is provably unaffected.
#
# Each workdir/ seeds the TARGET release's Cargo.lock over the PREVIOUS
# release's source tree. That skew is deliberate: the oracle needs the target
# lock, because the correct solution adds crates the previous release never
# named. But it means a candidate that has not done the work yet presents the
# previous release's manifest against the target's lock, so cargo re-resolves
# at grade time. Offline, re-resolving needs an index entry for every
# requirement that manifest names -- including the optional and dev-only ones
# this release dropped. Miss one and the workspace cannot build at all, and the
# gate is vetoed for packaging reasons rather than for behaviour.
#
# This lives outside assets/ on purpose: the fixture pin in test.sh digests
# /tests/assets, and this is verifier plumbing, not part of the scored corpus.
#
# No --locked: the previous release's own lock is not shipped anywhere in the
# task. Resolve it fresh here and freeze that answer into the image.
COPY prewarm-prev /opt/prewarm-prev
RUN cd /opt/prewarm-prev && cargo fetch

# From here on, nothing may reach the network. A slow or unavailable mirror must
# never be scored as a candidate failure.
ENV CARGO_NET_OFFLINE=true

COPY . /tests
RUN chmod +x /tests/test.sh && chmod -R a+rx /tests/assets/e2e 2>/dev/null || true

RUN mkdir -p /app /logs/verifier
WORKDIR /app
