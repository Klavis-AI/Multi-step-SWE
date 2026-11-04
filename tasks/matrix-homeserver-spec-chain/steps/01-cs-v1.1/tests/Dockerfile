# Verifier image for step 1.  Built with steps/01-cs-v1.1/tests/ as the Docker
# build context, so it cannot reach the task root or another step.  It installs
# its own toolchain -- the candidate must not be able to change how it is graded
# -- and carries every compiler and development library the task environment
# carries, so that a workspace which builds there also builds here.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash ca-certificates coreutils diffutils file gawk git make openssl \
        procps sqlite3 xxd \
        gcc g++ gdb pkg-config python3 python3-yaml \
        libssl-dev libsqlite3-dev libyaml-dev libcurl4-openssl-dev zlib1g-dev \
        libmagic-dev \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /work /logs/verifier

# The account a candidate's own processes run as.  `make` over the collected
# workspace, and the homeserver it produces, are candidate-written programs;
# running them as root would let a two-line Makefile read the scenario sources
# and the runner out of /tests before the first scored request is ever made.
# The scored transcripts are already gone by then -- verify.py reads them into
# memory and deletes them before it builds -- and this closes the rest.
RUN useradd --system --no-create-home --shell /usr/sbin/nologin mxbuild

COPY . /tests
RUN chmod +x /tests/test.sh

WORKDIR /app

# /tests belongs to root alone: the harness reads it, the candidate's processes
# cannot.  Root ignores mode bits, which is the point -- the verifier keeps
# working and `mxbuild` gets nothing.
RUN chmod -R go-rwx /tests && chmod 700 /tests
# collision-pad: keeps every sibling Dockerfile a distinct byte length, so docker's
# build-context sync (which calls a file unchanged when relpath+size+mtime match)
# can never hand this step's image a sibling step's copy of this file.
