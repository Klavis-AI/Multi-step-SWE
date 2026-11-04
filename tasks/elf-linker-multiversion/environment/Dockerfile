# Agent environment for the `wild` linker task.
#
# Carries exactly the toolchain the shipped contract names: a Rust toolchain to
# build the linker, clang-16 as the cross-capable assembler/compiler that
# produces the relocatable objects a linker consumes, the LLVM binutils to
# inspect ELF output, and qemu-user to execute non-native output. The verifier
# image installs the same set, so what the agent can reproduce locally is what
# is graded.
#
# It also carries the task's whole dependency ladder as a warm cargo registry,
# because the agent phase has no general network access and a build that cannot
# resolve a crate scores zero for a reason that has nothing to do with the
# linker. See the prefetch step below.
FROM rust:1.98-bookworm

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        clang-16 llvm-16 lld-16 \
        qemu-user-static \
        binutils file bsdmainutils xxd less vim-tiny procps \
        python3 \
    && rm -rf /var/lib/apt/lists/*

# Unsuffixed names for the tools the contract refers to by suffix-free name.
RUN for t in clang readelf objdump nm ar objcopy strip size strings; do \
        if [ -x "/usr/lib/llvm-16/bin/llvm-$t" ]; then \
            ln -sf "/usr/lib/llvm-16/bin/llvm-$t" "/usr/local/bin/llvm-$t-16"; \
        fi; \
    done; \
    ln -sf /usr/bin/clang-16 /usr/local/bin/clang-16

RUN rustup component add rustfmt clippy

# Dependency prefetch for the allowlisted agent phase.
#
# The agent works with no general network access, so any crate its build names
# has to be in the image already, or the build fails for a reason that has
# nothing to do with the linker it is writing. assets/prefetch/NN/ holds one
# step's dependency set as a workspace of stand-ins: that step's manifests with
# a stub src/ in place of every first-party source, so `cargo fetch` resolves
# and downloads the dependency graph and nothing else.
#
# One shared CARGO_HOME accumulates the union, which is why the whole ladder is
# fetched here rather than one step's set: this image serves all nine steps.
# The stand-ins are deleted once the fetch has run, and the first-party stubs
# are path members -- cargo does not copy local packages into the registry, so
# what remains is crates.io content only. Permissions are widened afterwards so
# that a build as a non-root user is not blocked by a cache it did not create.
COPY assets/prefetch /opt/prefetch
RUN set -eux; \
    for d in /opt/prefetch/*/; do \
        cd "$d"; \
        cargo fetch --locked; \
    done; \
    cd /; \
    rm -rf /opt/prefetch; \
    chmod -R a+rX "$CARGO_HOME"; \
    find "$CARGO_HOME" -type d -exec chmod a+w {} +

WORKDIR /app
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh
