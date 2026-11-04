# Verifier image for step 05 (RISC-V, CREL and the version-script grammar).
#
# Built with steps/05-riscv-crel-and-version-scripts/tests/ as the ONLY build context, so everything the
# grader needs is here: the toolchain that produces the relocatable objects the
# cases feed to the linker, the tools that inspect its output, the emulators
# that execute non-native output, and a prewarmed dependency graph so building
# the candidate's linker fits inside the step's verifier timeout.
FROM rust:1.98-bookworm

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        clang-16 llvm-16 lld-16 \
        qemu-user-static \
        binutils file bsdmainutils xxd \
        python3 \
    && rm -rf /var/lib/apt/lists/*

# The same unsuffixed-name symlinks the agent environment creates. Debian's
# llvm-16 package installs most tools as /usr/bin/llvm-<tool>-16 but not all of
# them -- `llvm-readelf` in particular only exists under /usr/lib/llvm-16/bin.
# Without this loop the case library's tool resolver falls through to the
# binutils spelling, or to nothing at all, and the grader inspects output with a
# different tool than the one the contract names. Keep this identical to
# environment.Dockerfile: what the agent can reproduce locally must be what is
# graded.
RUN for t in clang readelf objdump nm ar objcopy strip size strings; do \
        if [ -x "/usr/lib/llvm-16/bin/llvm-$t" ]; then \
            ln -sf "/usr/lib/llvm-16/bin/llvm-$t" "/usr/local/bin/llvm-$t-16"; \
        fi; \
    done; \
    ln -sf /usr/bin/clang-16 /usr/local/bin/clang-16

# The step id, written as a LITERAL before the first COPY. Harbor builds this
# image through a generated docker-compose-build.yaml that declares no `args:`,
# so an ARG here could never be given a value and every step would claim to be
# the default. test.sh compares this file against its own compiled-in id and
# refuses to score if they disagree.
RUN echo "05" > /etc/harbor-step-id

ENV CARGO_HOME=/usr/local/cargo
ENV CARGO_TARGET_DIR=/opt/cargo-target

# Dependency prewarm: the tag's workspace manifests and lockfile with every
# first-party source replaced by a stub, so this compiles the dependency graph
# and nothing else. The final `find` deletes anything executable the stub build
# produced, so no artifact of this layer can be mistaken for the candidate's
# own linker.
COPY assets/prewarm /opt/prewarm
RUN cd /opt/prewarm \
 && cargo fetch --locked \
 && { cargo build --release --locked --jobs 4 >/tmp/prewarm.log 2>&1 \
      || cargo build --release --jobs 4 >>/tmp/prewarm.log 2>&1 \
      || tail -30 /tmp/prewarm.log; } \
 && find /opt/cargo-target/release -maxdepth 1 -type f -delete \
 && rm -rf /opt/cargo-target/release/.fingerprint/wild-* \
           /opt/cargo-target/release/.fingerprint/libwild-* \
           /opt/cargo-target/release/.fingerprint/wild_lib-* \
           /opt/cargo-target/release/.fingerprint/linker-utils-* \
           /opt/cargo-target/release/.fingerprint/linker-diff-* \
           /opt/cargo-target/release/.fingerprint/linker-layout-* \
           /opt/cargo-target/release/.fingerprint/linker-trace-* \
           /opt/prewarm \
 && ! test -e /opt/cargo-target/release/wild

COPY . /tests
RUN chmod +x /tests/test.sh /tests/lib/run-cases.sh && mkdir -p /app /logs/verifier
