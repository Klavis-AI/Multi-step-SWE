# Verifier image for one step, built with that step's tests/ as the Docker
# build context.  It reconstructs THIS STEP'S BASE from the pinned upstream
# source, builds it, and keeps it as the reference: both the correctness
# comparison and the speedup denominator are measured here, in the same
# container and on the same pinned core as the candidate, so the ratio is
# self-calibrating and does not depend on how fast the target machine is.
#
# This file is identical for every step.  What differs is
# tests/BASE_ROWS.stepNN (which families this step's base carries),
# tests/UNITS.stepNN (what is measured), tests/ISA_PATTERNS.stepNN (the
# capability envelope) and tests/STEP.stepNN (the step identity), each named
# for its step so the docker build context cannot serve one step's file to
# another.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates cmake coreutils diffutils g++ gcc git make \
       ninja-build patch procps python3 util-linux \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /logs/verifier /work

COPY . /tests
RUN chmod +x /tests/test.sh

# Step identity, for the binding check at the top of test.sh.
#
# Read from this step's own data file rather than written as a literal, so that
# this Dockerfile is byte-identical in every step and cannot itself be the file
# that gets served from a sibling's build-context cache entry.  The
# (path, size, mtime) key that the builder uses for a build context is why the
# per-step files carry their step number in their name: two files that share a
# name, a size and a second are indistinguishable to it, and the wrong
# milestone's test image then gets baked in without an obvious failure.
RUN cat /tests/STEP.step* > /etc/harbor-step-id

# This step's base tree: the pinned source minus this step's family and every
# later one, so it carries exactly the families earlier milestones restored.
# The source arrives as a tarball rather than a clone: the checks must not
# depend on a live network, and a slow mirror must not be counted as a
# candidate failure.
#
# /task_base is the same pin with every family stripped: the tree the candidate
# starts from at step 1.  It is what the envelope check compares a candidate's
# scoped files against to tell the files it wrote from the files it inherited --
# the same stripper, the same tarball, and the same build command the
# environment image uses for /app/avisynthplus, so for a candidate that has
# changed nothing the two are byte-identical, which is exactly what makes a
# no-op pass the envelope vacuously rather than by luck.
RUN . /tests/vendor/PIN \
    && echo "$AVS_TARBALL_SHA256  /tests/vendor/pin.tar.gz" | sha256sum -c - \
    && mkdir -p /pin \
    && tar -xzf /tests/vendor/pin.tar.gz -C /pin \
    && python3 /tests/vendor/mklevel.py /pin /base_tree $(cat /tests/BASE_ROWS.step*) \
    && python3 /tests/vendor/mklevel.py /pin /task_base \
    && rm -rf /pin

# The reference library for this step's base tree.  It is shipped prebuilt and
# checksummed rather than compiled here: both the correctness comparison and the
# speedup denominator measure it *in this container, on these cores*, so the
# ratio is still self-calibrating, and compiling it would add five minutes to
# every step's image build for a library that never changes between runs.  The
# source it was built from is the /base_tree above, from the same pinned
# tarball, with the project's own flags.
RUN mkdir -p /basebld/avs_core \
    && tar -xzf /tests/vendor/base-lib.step*.tar.gz -C /basebld \
    && test -e /basebld/avs_core/libavisynth.so

# The measurement driver, built once against the base tree's public C header.
# It takes a script path, so it carries no configuration of its own.
RUN gcc -O2 -o /tests/vendor/avsbench /tests/vendor/avsbench.c \
        -I /base_tree/avs_core/include -ldl

WORKDIR /app
