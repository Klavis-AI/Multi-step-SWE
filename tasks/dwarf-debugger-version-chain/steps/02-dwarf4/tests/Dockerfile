# Verifier image for this step.  Build context is this tests/ directory.
#
# Deliberately absent: llvm-symbolizer and all of LLVM, binutils' readelf /
# objdump / addr2line / nm, elfutils, gdb and gdbserver.  The expectations were
# recorded from those tools at authoring time and are shipped here as data, so
# nothing the candidate could shell out to exists at scoring time.
FROM debian:trixie-slim

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash ca-certificates coreutils diffutils findutils make python3 \
        openjdk-25-jdk-headless xz-utils zlib1g \
    && rm -rf /var/lib/apt/lists/* \
    && for t in readelf objdump addr2line nm strings size c++filt gdb gdbserver \
                eu-stack eu-readelf llvm-symbolizer; do rm -f "/usr/bin/$t"; done

RUN mkdir -p /app /logs/verifier /work /dw

# The fixtures are unpacked at the same absolute path they were built and
# recorded at, so DW_AT_comp_dir and every recorded module path resolve exactly
# as they did when the expectations were taken.
COPY fixtures.tar.xz /tmp/fixtures.tar.xz
RUN tar -xJf /tmp/fixtures.tar.xz -C /dw && rm /tmp/fixtures.tar.xz \
    && chmod -R a+rX /dw

COPY . /tests
RUN chmod +x /tests/test.sh && rm -f /tests/fixtures.tar.xz \
    && chmod -R go-rwx /tests

WORKDIR /app
ENV LC_ALL=C TZ=UTC
