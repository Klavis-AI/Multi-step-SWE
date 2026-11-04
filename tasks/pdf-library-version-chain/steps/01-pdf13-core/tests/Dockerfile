# Verifier image for step 01-pdf13-core. Built with steps/01-pdf13-core/tests/ as the
# Docker build context, so it cannot reach the task root or another step; shared
# helpers are duplicated per step for that reason.
#
# This image deliberately contains NO PDF implementation: no poppler-utils, no qpdf, no
# mupdf, no ghostscript, no pdfium, no python PDF module. The candidate's utilities are
# rebuilt here from its own C sources and are the only thing in this container that can
# read a PDF, so a submission that shelled out to a system PDF tool scores zero rather
# than scoring someone else's work.
#
# The toolchain is a C toolchain: `gcc` and no `g++`, so "written in C" is structural
# rather than a request. python3 is present for the scorer itself and is kept off the
# candidate's PATH (see runner.py).
#
# xxd is installed because the agent image has it: a Makefile that embeds data (font
# metrics, a glyph list) with `xxd -i` must build here exactly as it did there.
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash ca-certificates coreutils diffutils findutils gawk grep sed \
        make gcc binutils libc6-dev zlib1g-dev \
        python3 util-linux procps xxd \
    && rm -rf /var/lib/apt/lists/*

# /app is repopulated from the collected artifact and /logs/verifier is bind-mounted at
# run time; creating them keeps workdir valid at container start.
RUN mkdir -p /app /logs/verifier /run/corpus

COPY . /tests
RUN chmod +x /tests/test.sh

WORKDIR /app

# Build-context collision guard: sibling Dockerfiles can differ only in the step
# name in the header comment. Docker's context sync keys on (relpath, size, mtime),
# so equal sizes would leave only the mtime standing between one step's image and
# another's recipe -- and GNU tar flattens mtime to whole seconds when the package is
# shipped. This pad gives every per-step Dockerfile in this task a size no sibling
# shares.
# collision-pad: .
