# Verifier image for step 08 (08-default-provider-and-hello-randomisation), rustls 0.23.0.
#
# Built with steps/08-default-provider-and-hello-randomisation/tests/ as the Docker build context: it cannot
# reach the task root or another step, so every shared file is copied into each
# step. Harbor skips its usual tests upload here and runs /tests/test.sh from
# this image.
#
# The image build uses the network - it compiles the BoGo runner and warms a
# Cargo registry cache from this step's frozen lockfile. The grading run does
# not: by then the runner binary, its certificate fixtures, the shim config and
# the whole dependency graph are inside the image, and test.sh builds the
# candidate with CARGO_NET_OFFLINE=true on its first attempt.

# ---------------------------------------------------------------------------
# Stage 1 - the BoringSSL BoGo runner.
#
# Verifier-owned and pinned to the commit THIS STEP's upstream tree names in
# bogo/fetch-and-build (or bogo/runme at the newest pins). Eight distinct
# BoringSSL commits cover the thirteen steps and a runner cannot grade a shim
# from another era - the older runners take ShimConfiguration.ErrorMap values
# as strings and the newer ones as arrays, so a mismatched pair fails to parse
# its config at all. Runner and config ship as a matched pair, per step.
#
# Building it here rather than at grading time keeps a Go module download off
# the critical path of a scored run. golang:1.26-bookworm is go1.26.8, at or
# above the `go` directive of every runner on the ladder, so Go never pauses to
# download a 240 MB toolchain of its own.
# ---------------------------------------------------------------------------
FROM golang:1.26-bookworm AS bogo-runner

COPY assets/bogo/runner-src/ /checkout/
# `go test -c` is exactly how upstream builds it; the result is a self-contained
# test binary that needs no Go toolchain at grading time. The oldest pin needs
# GO111MODULE=off: its three in-tree patches rewrite imports to relative form,
# which is GOPATH-mode only, so the command is substituted per step.
RUN set -eu; \
    cd "/checkout/ssl/test/runner"; \
    go test -c -o /runner.test; \
    test -x /runner.test

# ---------------------------------------------------------------------------
# Stage 2 - the grading image.
# ---------------------------------------------------------------------------
FROM rust:1.98.1-bookworm

# The verifier owns its toolchain independently of environment/Dockerfile, so a
# candidate that modified its own workspace cannot change how it is graded.
# cmake and a C toolchain are here because upstream's own bogo crate names
# aws-lc-rs unconditionally from step 10 (`use rustls::crypto::aws_lc_rs::hpke`)
# and it cannot be feature-flagged away; openssl is the differential peer;
# setpriv (util-linux) drops candidate code to nobody.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash build-essential ca-certificates clang cmake coreutils diffutils \
       findutils gawk grep jq openssl perl pkg-config procps python3 ripgrep \
       sed tar util-linux xxd \
    && rm -rf /var/lib/apt/lists/*

# The Go toolchain, from the stage that already has it.
#
# Not for the runner -- that is a compiled binary by now -- but for cargo. At
# step 10 upstream's own `bogo/Cargo.toml` asks rustls for the `fips` feature
# unconditionally, which pulls aws-lc-fips-sys, whose build script stops with
# "Missing dependency: Go is required for FIPS." It is a feature `bogo`
# requests *of rustls*, so nothing on the build line can subtract it, and
# without Go every group at step 10 fails. Copied from the stage above rather
# than apt-installed: it is the same go1.26 the runner was built with,
# bookworm's own golang is older than some of these pins want, and the layer
# is already being pulled. A complete step-10 build with this present takes
# about a minute, so the faithful feature set is close to free.
COPY --from=bogo-runner /usr/local/go /usr/local/go
ENV PATH=/usr/local/go/bin:$PATH

# Assert rather than assume: a base image that ever stopped shipping one of
# these must fail the build loudly instead of failing a scored group silently.
RUN set -eu; \
    for t in bash awk sed grep find sort comm xargs head tail wc tr cmp diff \
             sha256sum timeout setpriv python3 openssl cargo rustc cmake cc go; do \
        command -v "$t" > /dev/null || { echo "verifier image lacks $t" >&2; exit 1; }; \
    done

ENV CARGO_TERM_COLOR=never LC_ALL=C LANG=C
ENV CARGO_HOME=/usr/local/cargo

# The runner binary AND its checkout: the runner reads certificate fixtures
# from its own working directory at run time, so the source tree travels with
# it. Test vector corpora that nothing on this ladder reads (hpke, kyber,
# spake2plus testdata) are excluded when the step is generated, not here.
COPY --from=bogo-runner /runner.test /opt/bogo/runner.test
COPY --from=bogo-runner /checkout /opt/bogo/checkout

# Warm the Cargo registry cache with THIS step's exact shipped dependency graph.
# assets/prefetch is a generated stand-in package that depends on every registry
# entry of the step's frozen Cargo.lock at an exact `=version`, so the download
# set is the step's graph and nothing else. It is never shown to the agent and
# contains no implementation.
COPY assets/prefetch /opt/prefetch
RUN cd /opt/prefetch && cargo fetch && rm -rf /opt/prefetch/target

# The candidate's build runs as nobody, and what `cargo fetch` unpacked here it
# unpacked as root with whatever modes the published .crate carried. A handful
# of old crates ship source files with no group or other read bit -- fnv 1.0.7
# is one -- and the symptom is not a permission message anyone would connect to
# packaging:
#
#     error: couldn't read .../fnv-1.0.7/lib.rs: Permission denied (os error 13)
#     error: could not compile `fnv`
#
# which fails the build and therefore every group at that step. The base rust
# image opens up CARGO_HOME for exactly this reason; it just did so before this
# layer existed. Directories get the write bit as well, so a crate that is
# still only a .crate here can be unpacked by the uid that needs it.
RUN chmod -R a+rX "$CARGO_HOME/registry" \
    && find "$CARGO_HOME/registry" -type d -exec chmod a+w {} +

# /app is repopulated from the collected artifact and /logs/verifier is bind
# mounted at run time; creating them keeps workdir valid at container start.
RUN mkdir -p /app /logs/verifier

# Step identity, for the binding check at the top of test.sh.
#
# Written BEFORE the COPY below, so it names this IMAGE even when the copy is
# served from another step's cache entry. The builder identifies a build-context
# file by (relative path, size, mtime) and never reads its bytes, so two steps
# whose tests/ trees share a filename, a size and a second are indistinguishable
# to it -- and the wrong milestone's verifier gets baked in at an
# ordinary-looking score. Nothing inside a Dockerfile prevents that; this only
# reports it.
#
# Written as a LITERAL, substituted when this file is generated, rather than as
# an ARG: harbor builds the verifier through docker-compose-build.yaml, which
# declares no `args:`, so no --build-arg can reach this build and an ARG default
# would be an override surface nothing can use.
#
# Keep this number, the directory's number and test.sh's require_step equal.
RUN echo "08" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh /tests/bin/*.py 2>/dev/null || true

WORKDIR /app
